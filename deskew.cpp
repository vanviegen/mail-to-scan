// sudo apt install libopencv-imgproc-dev libopencv-highgui-dev libopencv-photo-dev libopencv-core-dev
// g++ -O2 -std=c++11 -lopencv_imgproc -lopencv_highgui -lopencv_photo -lopencv_core -lopencv_imgcodecs -o deskew deskew.cpp

#include <iostream>
#include <vector>
#include <stdlib.h>

#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/photo/photo.hpp>

using namespace cv;
using std::vector;

Vec2f findBottom(Mat img)
{
	const auto size = img.size();

	size_t yCount = size.height / 2.1;

	int skewMaxPixels = size.width * 0.15;
	int skewCount = skewMaxPixels * 2 + 1;
	int xMiddle = size.width / 2;

	float *result = (float *)calloc(1, sizeof(float) * yCount * skewCount);

	for (size_t y = 0; y < yCount; y++) {
		const uchar *startPixel = img.ptr<uchar>(y);
		const uchar *endPixel = startPixel + size.width;
		for (const uchar *pixel = startPixel; pixel < endPixel; pixel++) {
			if (*pixel) {
				float offsetDelta = (float)(pixel - startPixel - xMiddle) / size.width;
				float offset = -skewMaxPixels * offsetDelta;

				for (int skew = 0; skew < skewCount; skew++) {
					int yMiddle = cvRound(y + offset);
					if (yMiddle >=0 && yMiddle < yCount) {
						result[yMiddle * skewCount + skew]++;
					}
					offset += offsetDelta;
				}
			}
		}
	}

	Mat m1(yCount, skewCount, CV_32FC1, result);
	Mat m2 = Mat::zeros(yCount, skewCount, CV_32FC1);
	GaussianBlur(m1, m2, Size(5, 5), 0);
	free(result);
	result = (float *)m2.ptr();

	float bestResult = 0;
	float bestSkew, bestY;
	int offset = yCount/5;
	for (size_t yMiddle = 0; yMiddle < yCount; yMiddle++) {
		for (int skew = 0; skew < skewCount; skew++) {
			float v = result[yMiddle * skewCount + skew] * (yCount-yMiddle+offset) / (yCount+offset);
			if (v > bestResult) {
				bestResult = v;
				bestSkew = - (skew - skewMaxPixels) / (float)size.width;
				bestY = yMiddle - bestSkew*xMiddle;
			}
		}
	}
	//std::cout << bestResult << " y=" << bestY  << " skew=" << bestSkew << "\n";

	if (bestResult < 10) {
		std::cerr << "no box found\n";
		exit(4);
	}
	
	//std::cout << y << " / " << skew << "\n";
	
	return Vec2f(bestY,bestSkew);
}

struct TripletIt
{
	Mat *mat;
	float *pixel;
	float *startPixel;
	float *endPixel;
	int row;
	int top, left;
	int height, width;
	TripletIt(Mat &m, int _top=0, int right=0, int bottom=0, int _left=0)
		: endPixel((float *)0 + 1)
		, pixel(0)
		, row(-1)
		, top(_top)
		, left(_left)
	{
		mat = &m;
		auto size = m.size();
		width = size.width - left - right;
		height = size.height - top - bottom;
	}

	bool next()
	{
		pixel += 3;
		if (pixel >= endPixel) {
			if (++row >= height) return false;
			pixel = startPixel = mat->ptr<float>(top+row) + 3*left;
			endPixel = startPixel + 3*width;
		}
		return true;
	}
	
	inline int x() { return pixel-startPixel; }
	inline int y() { return row; }
	inline float &one() { return *(pixel+0); }
	inline float &two() { return *(pixel+1); }
	inline float &three() { return *(pixel+2); }
};

void removeWhiteGradient(Mat &orig)
{
	const int grid = 16;
	const int size = grid*16;
	const auto origSize = orig.size();

	Mat tmp1;
	resize(orig, tmp1, Size(size,size), 0, 0, INTER_AREA);
	Mat tmp2;
	tmp1.convertTo(tmp2, CV_8UC3, 256.0);
	

	tmp1.create(Size(grid,grid), CV_8UC3);
	tmp1 = Scalar(0,0,0);
	int best = 0;

	for(int y=0; y<size; y++) {
		auto row = tmp2.ptr<uint8_t>(y);
		auto tmp1Row = tmp1.ptr<uint8_t>(y*grid/size);
		for(int x=0; x<size; x++) {
			auto pixel = row + x*3;
			auto tmp1Pixel = tmp1Row + (x*grid/size)*3;
			int total = (int)pixel[0] + pixel[1] + pixel[2];
			if (total > (int)tmp1Pixel[0] + tmp1Pixel[1] + tmp1Pixel[2]) {
				tmp1Pixel[0] = pixel[0];
				tmp1Pixel[1] = pixel[1];
				tmp1Pixel[2] = pixel[2];
				if (total > best) best = total;
			}
		}
	}

	Mat mask;
	mask.create(Size(grid,grid), CV_8U);
	for(int y=0; y < grid; y++) {
		auto maskRow = mask.ptr<uint8_t>(y);
		auto tmp1Row = tmp1.ptr<uint8_t>(y);
		for(int x=0; x < grid; x++) {
			int total = (int)tmp1Row[x*3] + tmp1Row[x*3+1] + tmp1Row[x*3+2];
			maskRow[x] = (total < best/2) ? 255 : 0;
			if (maskRow[x]) std::cout << "mask " << x << "," << y << "\n";
		}
	}

	inpaint(tmp1, mask, tmp2, 3, INPAINT_TELEA);
	tmp2.convertTo(tmp1, CV_32FC3, 1.0/256);
	GaussianBlur(tmp1, tmp2, Size(5,5), 0);
	
	resize(tmp2, tmp1, origSize);

	int byteCount = origSize.width * 3;
	for (int y = 0; y < origSize.height; y++) {
		auto whiteRow = tmp1.ptr<float>(y);
		auto origRow = orig.ptr<float>(y);
		for (int byte = 0; byte < byteCount; byte++) {
			origRow[byte] /= (whiteRow[byte]-0.05);
		}
	}
}


int main(int argc, const char *argv[]) {
	if (argc != 3) {
		std::cerr << "usage: " << argv[0] << " in.jpg out.jpg\n";
		exit(1);
	}

	Mat img1 = imread(argv[1], 1);
	Mat orig = img1.clone();

	auto size = img1.size();
	float scale = 256.0 / std::max(size.height, size.width);

	Mat img2;
	resize(img1, img2, Size(0,0), scale, scale);
	cvtColor(img2, img1, COLOR_BGR2GRAY);
	if (mean(img2)[0] < 0.07*255) {
		std::cout << "image too dark\n";
		exit(2);
	}
	GaussianBlur(img1, img2, Size(5, 5), 0);
	Canny(img2, img1, 75, 200);

	Point2f origins[4];
	Point2f directions[4];

//	std::cout << img1.size().width << " x " <<img1.size().height << "\n";

	for(int i=0; i<4; i++) {
		auto v = findBottom(img1);
//		std::cout << v[0] << " " << v[1] << "\n";
		origins[i] = Point2f(0, v[0]);
		directions[i] = Point2f(1, v[1]);
		int oldWidth = img1.size().width;
		for(int j=0; j<=i; j++) {
			float x = origins[j].x;
			origins[j].x = origins[j].y;
			origins[j].y = oldWidth - 1 - x;

			x = directions[j].x;
			directions[j].x = directions[j].y;
			directions[j].y = -x;
		}
		// rotate counter-clock-wise
		transpose(img1, img2);
		flip(img2, img1, 0);
	}

	float unscale = 1.0/scale;

	Point2f src[4]; // tl, tr, br, bl
	for(int i=0; i<4; i++) {
		int j = (i+3)%4;
		Point2f x = origins[j] - origins[i];
		float cross = directions[i].x*directions[j].y - directions[i].y*directions[j].x;
		double t1 = (x.x * directions[j].y - x.y * directions[j].x)/cross;
		src[i] = (origins[i] + directions[i] * t1) * unscale;
//		std::cout << "origin=(" << origins[i].x << "," << origins[i].y << ") direction=(" << directions[i].x << "," << directions[i].y;
//		std::cout << ") intersect=(" << src[i].x << "," << src[i].y << ")\n";
	}

	/*
	for( size_t i = 0; i < 4; i++ )
	{
		line(orig, src[i], src[(i+1)%4], Scalar(255,255,255), 1, CV_AA);
		line(orig, origins[i] * unscale, origins[i] * unscale + directions[i] * (float)1000, Scalar(255,0,0), 1, CV_AA);
	}
	imwrite(argv[2], orig);
	exit(0);
	*/
	
	auto newWidth = std::max( norm(src[0]-src[1]), norm(src[2]-src[3]) );
	auto newHeight = std::max( norm(src[1]-src[2]), norm(src[3]-src[0]) );

	if (newWidth < size.width*0.7 && newHeight < size.height*0.7) {
		std::cerr << "contours too small\n";
		exit(3);
	}
	
	Point2f dst[] = {
		Point(0, 0),
		Point(newWidth-1, 0),
		Point(newWidth-1, newHeight-1),
		Point(0, newHeight-1)
	};

	auto M = getPerspectiveTransform(src, dst);
	orig.convertTo(img2, CV_32F, 1.0/256);
	warpPerspective(img2, img1, M, Size(newWidth, newHeight));

	img2 = img1;

	removeWhiteGradient(img2);

	float darkest = 0.75;
	const auto psize = img2.size();
	int border = (psize.height < psize.width ? psize.height : psize.width) / 10;
	TripletIt it(img2, border, border, border, border);
	while(it.next()) {
		float v = min(min(it.one(), it.two()), it.three());
		if (v<darkest) darkest = v;
	}
	darkest += 0.05;
	if (darkest < 0) darkest = 0;

	float mult = 1.0 / (1.0-darkest);

	it = TripletIt(img2);
	while(it.next()) {
		it.one() = (it.one()-darkest) * mult;
		it.two() = (it.two()-darkest) * mult;
		it.three() = (it.three()-darkest) * mult;
	}

	img1.convertTo(img2, CV_8U, 256);
	
	imwrite(argv[2], img2);
}
