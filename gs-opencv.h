#include <opencv2/imgproc/types_c.h>

using namespace std; 
using namespace cv;

struct g_point {
	int x;
	int y;
	int w;
	Mat match;
	friend std::ostream& operator <<(std::ostream& os, g_point const& a) {
		return os << "[ " << a.x << " , " << a.y << " , " << a.w << " ] ";
	}
};

g_point MatchingMethod(Mat src, Mat tpl) {
	int match_method = 0;
	Mat img_display = src;
	Mat result;
	// src.copyTo(img_display); //将src的内容拷贝到img_display
	 // 创建输出结果的矩阵
	int result_cols = src.cols - tpl.cols + 1;     //计算用于储存匹配结果的输出图像矩阵的大小。
	int result_rows = src.rows - tpl.rows + 1;
	result.create(result_cols, result_rows, CV_32FC1);//被创建矩阵的列数，行数，以CV_32FC1形式储存。
	// 进行匹配和标准化
	matchTemplate(src, tpl, result, match_method); //待匹配图像，模版图像，输出结果图像，匹配方法（由滑块数值给定。）
	normalize(result, result, 0, 1, NORM_MINMAX, -1, Mat());//输入数组，输出数组，range normalize的最小值，range normalize的最大值，归一化类型，当type为负数的时候输出的type和输入的type相同。
	// 通过函数 minMaxLoc 定位最匹配的位置 
	double minVal; double maxVal; Point minLoc; Point maxLoc;
	Point matchLoc;
	minMaxLoc(result, &minVal, &maxVal, &minLoc, &maxLoc, Mat());//用于检测矩阵中最大值和最小值的位置
	// 对于方法 SQDIFF 和 SQDIFF_NORMED, 越小的数值代表更高的匹配结果. 而对于其他方法, 数值越大匹配越好
	if (match_method == TM_SQDIFF || match_method == TM_SQDIFF_NORMED) {
		matchLoc = minLoc;
	}
	else {
		matchLoc = maxLoc;
	}
	g_point p;
	p.x = matchLoc.x + tpl.cols / 2;
	p.y = matchLoc.y + tpl.rows / 2;
	p.w = tpl.cols;

	// 绘制匹配得到区域
	//rectangle(img_display, matchLoc, Point(matchLoc.x + tpl.cols, matchLoc.y + tpl.rows), Scalar(0, 0, 255), 2, 8, 0); //将得到的结果用矩形框起来
	//rectangle(result, matchLoc, Point(matchLoc.x + tpl.cols, matchLoc.y + tpl.rows), Scalar(0, 0, 255), 2, 8, 0);

	//获取匹配得到区域
	Rect img_ROI = Rect(matchLoc, Point(matchLoc.x + tpl.cols, matchLoc.y + tpl.rows));
	Mat img = src.clone();
	Mat ROI_img = img(img_ROI);
	p.match = ROI_img;
	//p.match = src;
	return p;
}

// 判断是否为黄色
bool matchYellow(Mat image) {
	int line = image.rows * image.cols * 0.2;
	int numOfyellow = 0;            //记录颜色为黄色的像素点
	for (int i = 0; i < image.rows; i++)
	{
		for (int j = 0; j < image.cols; j++)   //遍历图片的每一个像素点
		{
			if ((image.at<Vec3b>(i, j)[0] <= 120 && image.at<Vec3b>(i, j)[1] >= 170 && image.at<Vec3b>(i, j)[2] >= 230)
				|| (image.at<Vec3b>(i, j)[0] > 120 && image.at<Vec3b>(i, j)[0] <= 180 && image.at<Vec3b>(i, j)[1] >= 180 &&
					image.at<Vec3b>(i, j)[2] >= 220)) {//对该像素是否为黄色进行判断
				numOfyellow++;
				if (numOfyellow > line) return true;
			}
		}
	}
	return false;
}

// Mat C4转C3
Mat matC4ToC3(Mat src) {
	Mat src_rgb;
	cvtColor(src, src_rgb, COLOR_RGBA2RGB); // C4转C3   
	return src_rgb;
}

//直方图比对
double compareFacesByHist(Mat img, Mat orgImg)
{
	Mat tmpImg;
	resize(img, tmpImg, Size(orgImg.cols, orgImg.rows));
	//HSV颜色特征模型(色调H,饱和度S，亮度V)
	cvtColor(tmpImg, tmpImg, COLOR_BGR2HSV);
	cvtColor(orgImg, orgImg, COLOR_BGR2HSV);
	//直方图尺寸设置
	//一个灰度值可以设定一个bins，256个灰度值就可以设定256个bins
	//对应HSV格式,构建二维直方图
	//每个维度的直方图灰度值划分为256块进行统计，也可以使用其他值
	int hBins = 256, sBins = 256;
	int histSize[] = { hBins,sBins };
	//H:0~180, S:0~255,V:0~255
	//H色调取值范围
	float hRanges[] = { 0,180 };
	//S饱和度取值范围
	float sRanges[] = { 0,255 };
	const float* ranges[] = { hRanges,sRanges };
	int channels[] = { 0,1 };//二维直方图
	MatND hist1, hist2;
	calcHist(&tmpImg, 1, channels, Mat(), hist1, 2, histSize, ranges, true, false);
	normalize(hist1, hist1, 0, 1, NORM_MINMAX, -1, Mat());
	calcHist(&orgImg, 1, channels, Mat(), hist2, 2, histSize, ranges, true, false);
	normalize(hist2, hist2, 0, 1, NORM_MINMAX, -1, Mat());
	double similarityValue = compareHist(hist1, hist2, CV_COMP_CORREL);
	return 1 - similarityValue;
}

// 计算两张图像的PSNR相似度
double getPSNR(const Mat& I1, const Mat& I2) {
	Mat s1;
	absdiff(I1, I2, s1);       // |I1 - I2|
	s1.convertTo(s1, CV_32F);  // cannot make a square o	n 8 bits
	s1 = s1.mul(s1);           // |I1 - I2|^2

	Scalar s = sum(s1);         // sum elements per channel

	double sse = s.val[0] + s.val[1] + s.val[2]; // sum channels

	if (sse <= 1e-10) // for small values return zero
		return 0;
	else
	{
		double  mse = sse / (double)(I1.channels() * I1.total());
		double psnr = 10.0 * log10((255 * 255) / mse);
		return psnr;
	}
}

// 识别src图片中是否包含tpl模板
bool isExist(Mat src, Mat tpl, double diff, bool debug) {
	g_point p = MatchingMethod(src, tpl); // 模板匹配
	if (debug) {
		imwrite("debug_isExist.jpg", p.match);
	}
	double d = compareFacesByHist(tpl, p.match);
	double psnr = getPSNR(tpl, p.match);
	if (debug) {
		cout << "diff：" << d << endl;
		cout << "psnr：" << psnr << endl;
	}
	if (d < diff) {
		return TRUE;
	}
	return FALSE;
}

// 比较图片是否相同
bool isMatMatch(Mat src, Mat tpl, int val, double diff, bool debug) {
	if (src.rows != tpl.rows || src.cols != tpl.cols) {
		return FALSE;
	}
	int dims = src.channels();
	dims = 1;
	int equalCount = 0;
	for (int i = 0; i < src.rows; i++) {
		for (int j = 0; j < src.cols; j++) {
			if (dims == 1) {
				int srcVal = (int)src.at<uchar>(i, j);
				int tplVal = (int)tpl.at<uchar>(i, j);
				//cout << "srcVal：" << srcVal << "，tplVal：" << tplVal << endl;
				if (abs(srcVal - tplVal) <= val) {
					equalCount++;
				}
			}
			if (dims == 3) {
				Vec3b srcBgr = src.at<Vec3b>(i, j);
				Vec3b tplBgr = tpl.at<Vec3b>(i, j);
				//cout << "srcVal：" << (int)srcBgr[0] << "，tplVal：" << (int)tplBgr[0] << "srcVal：" << (int)srcBgr[1] << "，tplVal：" << (int)tplBgr[1] << "srcVal：" << (int)srcBgr[2] << "，tplVal：" << (int)tplBgr[2] << endl;
				for (int k = 0; k < 3; k++) {
					if (abs(srcBgr[k] - tplBgr[k]) <= val) {
						equalCount++;
					}
				}
			
			}
		
		}
	}
	double d = 1.0 - equalCount * 1.0 / src.rows / src.cols / dims;
	//double d = compareFacesByHist(src, tpl);
	if (debug) {
		cout << "diff：" << d << endl;
	}
	return d <= diff;
}

// 绘制指定区域
void drawMat(Mat src, RECT rect) {
	Point p1 = Point(rect.left, rect.top);
	Point p2 = Point(rect.right, rect.bottom);
	cout << p1 << "||" << p2 << endl;
	rectangle(src, p1, p2, Scalar(0, 0, 255), 2, 8, 0);
}

// 截图指定区域的图片
Mat getMatByRect(Mat src, RECT rect) {
	Rect roi = Rect(Point(rect.left, rect.top), Point(rect.right, rect.bottom));
	Mat img = src.clone();
	return img(roi);
}

// 打印Mat的通道数量、像素
void printMat(Mat src) {
	int dims = src.channels();//image的通道
	cout << "dims：" << dims << endl;
	int h = src.rows; //image的行
	int w = src.cols; //image的列
	for (int row = 0; row < h; row++) //遍历image的所有行
	{
		for (int col = 0; col < w; col++) //遍历image的所有列
		{
			if (dims == 1) //单通道的灰度图像
			{
				int pv = src.at<uchar>(row, col);//得到行、列像素值
				//src.at<uchar>(row, col) = 255 - pv;//给像素值重新赋值（与原来像素相反）
				cout << pv << " | ";
			}
			if (dims == 3) //三通道的彩色图像
			{
				Vec3b bgr = src.at<Vec3b>(row, col); //opencv特定的类型，获取三维颜色，3个值
				//src.at<Vec3b>(row, col)[0] = 255 - bgr[0];//代表image的第一通道
				//src.at<Vec3b>(row, col)[1] = 255 - bgr[1];
				//src.at<Vec3b>(row, col)[2] = 255 - bgr[2];//对彩色图像读取它的像素值，并且对像素值进行改写。
				cout << (int)bgr[0] << "," << (int)bgr[1] << "," << (int)bgr[2] << " | ";
			}
		}
		cout << endl;
	}
}