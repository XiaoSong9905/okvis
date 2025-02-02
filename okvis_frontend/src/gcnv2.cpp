#include <cassert>
#include <string>
#include <vector>
#include <memory>
#include <opencv2/opencv.hpp>
#include <okvis/gcnv2.hpp>
#include <torch/script.h>

namespace gcnv2
{

GCNv2DetectorDescriptor::GCNv2DetectorDescriptor( int img_height, int img_width ) : \
    img_height( img_height ), img_width( img_width )
{
    // Load model
    initTorch( );
}


GCNv2DetectorDescriptor::~GCNv2DetectorDescriptor() {}


void GCNv2DetectorDescriptor::initTorch( )
{
    // Set export GCNV2_TORCH_MODEL_PATH="/path/to/model.pt"
    std::string model_filename( std::getenv("GCNV2_TORCH_MODEL_PATH") );

    // Try load model
    try
    {
        // Deserialize the Module from a file using torch::jit::load().
        torch_model = torch::jit::load( model_filename );
        torch_model->to( torch::kCUDA );
    }
    catch ( std::exception& e )
    {
        printf("Error loading model. [%s]\n", e.what() );
        exit(-1);
    }
}


void GCNv2DetectorDescriptor::detectAndCompute( cv::InputArray _image, \
                                                cv::InputArray _mask, \
                                                std::vector<cv::KeyPoint>& _keypoints, \
                                                cv::OutputArray& _descriptors, \
                                                bool _useProvidedKeypoints )
{
    // Currently don't support provided keypoint
    if ( _useProvidedKeypoints || _keypoints.size() )
    {
        throw std::runtime_error("GCNv2DetectorDescriptor::detectAndCompute do not support provided keypoint\n" );
    }

    if ( !_mask.empty() )
    {
        throw std::runtime_error("GCNv2DetectorDescriptor::detectAndCompute do not support customize mask\n");
    }

    // No input image, return
    if ( _image.empty() )
    {
        return;
    }

    // Convert image to gray scale
    cv::Mat _gray_image_fp32 = _image.getMat();
    if ( _gray_image_fp32.type() != CV_8UC1 && _gray_image_fp32.type() != CV_32FC1 )
    {
        cv::cvtColor( _gray_image_fp32, _gray_image_fp32, cv::COLOR_BGR2GRAY );
    }

    // Convert image to fp32
    if ( _gray_image_fp32.type() == CV_8UC1 )
    {
        _gray_image_fp32.convertTo( _gray_image_fp32, CV_32FC1, 1.0f / 255.0f , 0 );
    }

    // Resize image
    // Model only accept fixed size
    float width_ratio = _gray_image_fp32.size().width /  (float) img_width;
    float height_ratio = _gray_image_fp32.size().height / (float) img_height;
    if ( _gray_image_fp32.size().height != img_height || _gray_image_fp32.size().width != img_width )
    {
        cv::resize( _gray_image_fp32, _gray_image_fp32, cv::Size( img_width, img_height ) );
    }

    // Run DL model and extracto keypoints
    detectAndComputeTorch( _gray_image_fp32, _keypoints, _descriptors, width_ratio, height_ratio );
}


static void NonMaximalSuppression( cv::Mat kpts_raw, \
                                   cv::Mat desc_raw, \
                                   std::vector<cv::KeyPoint>& _keypoints, \
                                   cv::Mat& descriptors, \
                                   int dist_threshold, \
                                   int img_width, int img_height, \
                                   float width_ratio, float height_ratio)
{
    cv::Mat kpt_grid  = cv::Mat( cv::Size( img_width, img_height ), CV_8UC1 );
    cv::Mat kpt_index = cv::Mat( cv::Size( img_width, img_height ), CV_16UC1 );
    kpt_grid.setTo( 0 );
    kpt_index.setTo( 0 );

    for ( int i = 0; i < kpts_raw.rows; ++i )
    {
        int u = (int) kpts_raw.at<float>(i, 0);
        int v = (int) kpts_raw.at<float>(i, 1);
        kpt_grid.at<char>(v, u) = 1;
        kpt_index.at<unsigned short>(v, u) = i;
    }

    cv::copyMakeBorder( kpt_grid, kpt_grid, dist_threshold, dist_threshold,
                        dist_threshold, dist_threshold, cv::BORDER_CONSTANT, 0 );

    for ( int i = 0; i < kpts_raw.rows; ++i )
    {
        int u = (int) kpts_raw.at<float>(i, 0) + dist_threshold;
        int v = (int) kpts_raw.at<float>(i, 1) + dist_threshold;

        if ( kpt_grid.at<char>(v, u) != 1 )
            continue;

        for ( int j = -dist_threshold; j <= dist_threshold; ++j )
            for ( int k = -dist_threshold; k <= dist_threshold; ++k )
            {
                if ( j == 0 && k == 0 )
                    continue;

                kpt_grid.at<char>( v + j, u + k ) = 0;
            }

        kpt_grid.at<char>(v, u) = 2;
    }

    std::vector<int> valid_idxs;
    for ( int u = dist_threshold; u < img_width - dist_threshold; ++u )
        for ( int v = dist_threshold; v < img_height - dist_threshold; ++v )
        {
            if (kpt_grid.at<char>(v, u) == 2) {
                int idx = (int) kpt_index.at<unsigned short>( v - dist_threshold, u - dist_threshold );
                int x = kpts_raw.at<float>(idx, 0);
                int y = kpts_raw.at<float>(idx, 1);
                _keypoints.push_back( cv::KeyPoint( x * width_ratio, y * height_ratio, 1.0f ) );
                valid_idxs.push_back(idx);
            }
        }

    descriptors.create(valid_idxs.size(), 32, CV_8U);

    for ( size_t i = 0; i < valid_idxs.size(); ++i )
    {
        for ( size_t j = 0; j < 32; ++j )
        {
            descriptors.at<unsigned char>(i, j) = desc_raw.at<unsigned char>(valid_idxs[i], j);
        } 
    }

}


void GCNv2DetectorDescriptor::detectAndComputeTorch( cv::Mat& _gray_image_fp32, \
                                                     std::vector<cv::KeyPoint>& _keypoints, \
                                                     cv::OutputArray& _descriptors, \
                                                     float width_ratio, float height_ratio )
{
    // Stack the gray scale image in the way model expects it
    int dist_threshold = 20; // TODO: this should be a config param based on img height and img width

    // Convert OpenCV data to torch compatable data type
    static std::vector<int64_t> dims = {1, img_height, img_width, 1};
    std::vector<torch::jit::IValue> inputs_torch;

    auto input_torch = torch::from_blob( _gray_image_fp32.data, dims, torch::kFloat32 ).to( torch::kCUDA );
    input_torch = input_torch.permute({0, 3, 1, 2});
    inputs_torch.push_back( input_torch );

    // Run model
    auto outputs_torch = torch_model->forward(inputs_torch).toTuple();

    // Extract output
    torch::Tensor pts  = outputs_torch->elements()[0].toTensor().squeeze().to(torch::kCPU);
    torch::Tensor desc = outputs_torch->elements()[1].toTensor().squeeze().to(torch::kCPU);

    cv::Mat kpts_raw(cv::Size(3,  pts.size(0)), CV_32FC1, pts.data<float>());
    cv::Mat desc_raw(cv::Size(32, pts.size(0)), CV_8UC1, desc.data<unsigned char>());
    cv::Mat descriptors; // descriptors after applying non-maximal suppersion on keypoints

    NonMaximalSuppression(kpts_raw, desc_raw, _keypoints, descriptors, \
        dist_threshold, img_width, img_height, width_ratio, height_ratio );

    int num_kpts = _keypoints.size();
    _descriptors.create(num_kpts, 32, CV_8U);
    descriptors.copyTo(_descriptors.getMat());
}


cv::String GCNv2DetectorDescriptor::getDefaultName() const
{
    return (cv::FeatureDetector::getDefaultName() + ".GCNv2DetectorDescriptor");
}


} // namespace GCNv2
