/*
 * ConversionFilter.cpp
 *
 *  Created on: 15.03.2013
 *      Author: poschmann
 */

#include "imageprocessing/filtering/ConversionFilter.hpp"

using cv::Mat;

namespace imageprocessing {
namespace filtering {

ConversionFilter::ConversionFilter(int type, double alpha, double beta) : type(type), alpha(alpha), beta(beta) {}

Mat ConversionFilter::applyTo(const Mat& image, Mat& filtered) const {
	image.convertTo(filtered, type, alpha, beta);
	return filtered;
}

void ConversionFilter::applyInPlace(Mat& image) const {
	applyTo(image, image);
}

} /* namespace filtering */
} /* namespace imageprocessing */
