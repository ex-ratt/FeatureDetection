/*
 * AggregatedFeaturesDetector.cpp
 *
 *  Created on: 22.10.2015
 *      Author: poschmann
 */

#include "classification/LinearKernel.hpp"
#include "detection/AggregatedFeaturesDetector.hpp"
#include "imageprocessing/Patch.hpp"
#include "imageprocessing/filtering/GrayscaleFilter.hpp"
#include <stdexcept>

using classification::LinearKernel;
using classification::SupportVectorMachine;
using cv::Point;
using cv::Rect;
using cv::Size;
using cv::Mat;
using imageprocessing::ImagePyramid;
using imageprocessing::ImagePyramidLayer;
using imageprocessing::Patch;
using imageprocessing::VersionedImage;
using imageprocessing::extraction::AggregatedFeaturesExtractor;
using imageprocessing::filtering::ConvolutionFilter;
using imageprocessing::filtering::GrayscaleFilter;
using imageprocessing::filtering::ImageFilter;
using std::make_shared;
using std::pair;
using std::shared_ptr;
using std::vector;

namespace detection {

AggregatedFeaturesDetector::AggregatedFeaturesDetector(shared_ptr<ImageFilter> imageFilter, shared_ptr<ImageFilter> layerFilter,
		int cellSize, Size windowSize, int octaveLayerCount, shared_ptr<SupportVectorMachine> svm, shared_ptr<NonMaximumSuppression> nms,
		float widthScale, float heightScale, int minWindowWidth, int maxWindowWidth) : AggregatedFeaturesDetector(
				make_shared<AggregatedFeaturesExtractor>(imageFilter, layerFilter, windowSize, cellSize, octaveLayerCount,
						minWindowWidth, maxWindowWidth), svm, nms, widthScale, heightScale) {}

AggregatedFeaturesDetector::AggregatedFeaturesDetector(shared_ptr<ImageFilter> filter, int cellSize, Size windowSize,
		int octaveLayerCount, shared_ptr<SupportVectorMachine> svm, shared_ptr<NonMaximumSuppression> nms,
		float widthScale, float heightScale, int minWindowWidth, int maxWindowWidth) : AggregatedFeaturesDetector(
				make_shared<AggregatedFeaturesExtractor>(filter, windowSize, cellSize, octaveLayerCount,
						minWindowWidth, maxWindowWidth), svm, nms, widthScale, heightScale) {}

AggregatedFeaturesDetector::AggregatedFeaturesDetector(shared_ptr<AggregatedFeaturesExtractor> featureExtractor,
		shared_ptr<SupportVectorMachine> svm, shared_ptr<NonMaximumSuppression> nms, float widthScale, float heightScale) :
				featureExtractor(featureExtractor),
				nonMaximumSuppression(nms),
				kernelSize(svm->getSupportVectors()[0].size()),
				scoreThreshold(svm->getThreshold()),
				bias(-svm->getBias()),
				widthScale(widthScale),
				heightScale(heightScale) {
	if (!dynamic_cast<LinearKernel*>(svm->getKernel().get()))
		throw std::invalid_argument("AggregatedFeaturesDetector: the SVM must use a LinearKernel");
	convolutionFilter = make_shared<ConvolutionFilter>(CV_32F);
	convolutionFilter->setKernel(svm->getSupportVectors()[0]);
	convolutionFilter->setAnchor(Point(0, 0));
	convolutionFilter->setDelta(bias - scoreThreshold);
	scorePyramid = make_shared<ImagePyramid>(featureExtractor->getFeaturePyramid());
	scorePyramid->addLayerFilter(convolutionFilter);
}

vector<Rect> AggregatedFeaturesDetector::detect(shared_ptr<VersionedImage> image) {
	update(image);
	return detect();
}

vector<pair<Rect, float>> AggregatedFeaturesDetector::detectWithScores(shared_ptr<VersionedImage> image) {
	update(image);
	return detectWithScores();
}

void AggregatedFeaturesDetector::update(shared_ptr<VersionedImage> image) {
	featureExtractor->update(image);
	scorePyramid->update(image);
}

vector<Rect> AggregatedFeaturesDetector::detect() {
	vector<Detection> candidates = getPositiveWindows();
	vector<Detection> detections = nonMaximumSuppression->eliminateRedundantDetections(candidates);
	return extractBoundingBoxes(detections);
}

vector<pair<Rect, float>> AggregatedFeaturesDetector::detectWithScores() {
	vector<Detection> candidates = getPositiveWindows();
	vector<Detection> detections = nonMaximumSuppression->eliminateRedundantDetections(candidates);
	return extractBoundingBoxesWithScores(detections);
}

vector<Detection> AggregatedFeaturesDetector::getPositiveWindows() {
	vector<Detection> positiveBounds;
	for (const shared_ptr<ImagePyramidLayer>& layer : scorePyramid->getLayers()) {
		const Mat& scoreMap = layer->getScaledImage();
		int validHeight = scoreMap.rows - kernelSize.height + 1;
		int validWidth = scoreMap.cols - kernelSize.width + 1;
		for (int y = 0; y < validHeight; ++y) {
			for (int x = 0; x < validWidth; ++x) {
				float score = scoreMap.at<float>(y, x);
				if (score > 0) {
					Rect boundsInLayer = Rect(Point(x, y), kernelSize);
					Rect boundsInImage = featureExtractor->computeBoundsInImagePixels(boundsInLayer, *layer);
					Rect scaledBoundsInImage = rescaleWindow(boundsInImage);
					positiveBounds.push_back({score, scaledBoundsInImage});
				}
			}
		}
	}
	return positiveBounds;
}

Rect AggregatedFeaturesDetector::rescaleWindow(Rect bounds) const {
	Point center = Patch::computeCenter(bounds);
	Size rescaledSize(widthScale * bounds.width, heightScale * bounds.height);
	return Patch::computeBounds(center, rescaledSize);
}

vector<Rect> AggregatedFeaturesDetector::extractBoundingBoxes(vector<Detection> detections) {
	vector<Rect> boundingBoxes;
	boundingBoxes.reserve(detections.size());
	for (const Detection& detection : detections)
		boundingBoxes.push_back(detection.bounds);
	return boundingBoxes;
}

vector<pair<Rect, float>> AggregatedFeaturesDetector::extractBoundingBoxesWithScores(vector<Detection> detections) {
	vector<pair<Rect, float>> detectionsWithScores;
	detectionsWithScores.reserve(detections.size());
	for (Detection detection : detections)
		detectionsWithScores.push_back(std::make_pair(detection.bounds, detection.score + scoreThreshold));
	return detectionsWithScores;
}

float AggregatedFeaturesDetector::getScoreThreshold() const {
	return scoreThreshold;
}

void AggregatedFeaturesDetector::setScoreThreshold(float threshold) {
	scoreThreshold = threshold;
	convolutionFilter->setDelta(bias - scoreThreshold);
}

shared_ptr<AggregatedFeaturesExtractor> AggregatedFeaturesDetector::getFeatureExtractor() {
	return featureExtractor;
}

const shared_ptr<AggregatedFeaturesExtractor> AggregatedFeaturesDetector::getFeatureExtractor() const {
	return featureExtractor;
}

shared_ptr<ImagePyramid> AggregatedFeaturesDetector::getScorePyramid() {
	return scorePyramid;
}

const shared_ptr<ImagePyramid> AggregatedFeaturesDetector::getScorePyramid() const {
	return scorePyramid;
}

} /* namespace detection */
