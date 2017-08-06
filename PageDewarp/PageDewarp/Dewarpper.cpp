#include "stdafx.h"
#include "Dewarpper.h"
#include "Fitting.h"
#include <numeric>

enum ERROR_TYPE {SUCCESS, LAYOUTREC_FAILURE};

Dewarpper::Dewarpper() {
	/* 默认初始化
	*/
}

Dewarpper::Dewarpper(const char *fileName) : img(fileName) {
	/* 使用图片的文件名初始化
	*/
}

Dewarpper::~Dewarpper() {
	/* 析构
	*/
	if (bounds) delete bounds;
}

int Dewarpper::dewarp() {
	/* 执行图像矫正
	*/
	/* 目标是将图像先划分成若干文字块，针对每个文字块进行矫正，这样可以避免图表、分栏和标题行
	字号不同等问题的干扰。但为尽快实现一套基本能用的算法，先按文献中的方法操作，即假设书页是
	纯文本的，直接灰度、二值化->计算行高->提取文本线->图	像重构。（对靠近书脊边缘的文字，可能
	还需要进一步插值、矫正等。不过暂时也先不考虑这个问题。）
	bounds = LayoutRecognization();
	if (!bounds || (bounds->empty())) {
		return LAYOUTREC_FAILURE;
	}
	for (auto i : *bounds) {
		img.setBoundary(i);
		preProcedure();
		calcLineHeight();
		getTextLine();
		reshape();
	}
	*/
	preProcedure();
	int lineHt = calcLineHeight();
	getTextLine(lineHt);
	reshape();
	return 0;
}

void Dewarpper::save(const char *fileName) {
	/* 使用提供的文件名保存当前图像
	*/
	doSave(&img, fileName);
}

vector<PageImage::Boundary>* Dewarpper::LayoutRecognization() {
	/* 版面识别
	*/
	vector<PageImage::Boundary> *ret = new vector<PageImage::Boundary>;
	//////////////////////////////FixMe/////////////////////////////
	return ret;
}

int Dewarpper::preProcedure() {
	/* 预处理。在这一步进行灰度、二值化。如dewarp()函数的注释所述，由于假设图像是
	纯文本的，因此无版面识别操作，需要在这预处理部分裁剪掉图像的白边
	*/
	// 图像灰度化、二值化
	img.grayScale().binary();
	// 裁剪，去除白边
	auto vprj = img.vCountBlack(), hprj = img.hCountBlack();  // 将图像向行、列方向投影，统计黑色像素点个数
	int wid = img.getSize().first, ht = img.getSize().second;  // 获得图像宽、高
	int sumblk = 0, vThre, hThre;  // sumblk记录全图黑色像素点个数，vThre、hThre分别是竖直和水平方向裁剪的阈值
	for (int i = 0; i < wid; ++i) {
		sumblk += vprj[i];
	}
	double t = 0.3;  // 0.3是经验值。由于书页不可避免的弯曲、倾斜，用该值调整阈值可以避免将有效但处于边缘位置的文字切除
	vThre = (t * sumblk) / wid;
	hThre = (t * sumblk) / ht;
	int x1 = 0, y1 = 0, x2 = wid - 1, y2 = ht - 1; 
	while (vprj[x1] < vThre) ++x1;  // 按算得的阈值寻找裁剪的位置
	while (vprj[x2] < vThre) --x2;
	while (hprj[y1] < hThre) ++y1;
	while (hprj[y2] < hThre) --y2;
	img.setBoundary(x1, y1, x2, y2);  // 设置图像的活动区域，从而起“裁剪”的效果
	delete []vprj;
	delete []hprj;
	return 0;
}

int Dewarpper::calcLineHeight() {
	/* 计算行高。现在是手动定参数的，如何自动确定参数是难点。
	*/
	double avgLineHeight = 0.0;
	int elemWidth = 0, elemHeight = 0;  // 结构元素宽、高
	pair<int, int> elem;
	// 开运算，去除噪声点
	elemWidth = 4; elemHeight = 4;
	setElem(elem, elemWidth, elemHeight);
	PageImage ret = img.bwDilate(elem).bwErode(elem);
	doSave(&ret, "testOut/CHS001Open.tif");
	// 腐蚀一次，计算一次行高，当迭代的行高变化量小于结构元素高度或达到设置的次数上限时终止
	elemWidth = 20, elemHeight = 2;
	setElem(elem, elemWidth, elemHeight);
	int sy = 0, maxIter = 8;
	bool nextIter = true;
	while (nextIter && (sy++ < maxIter)) {
		// 腐蚀
		ret = ret.bwErode(elem);
		doSave(&ret, string("testOut/CHS001Erode" + to_string(sy) + ".tif").c_str());
		// 选取页面的1/3、1/2、2/3三列计算各行高
		double newAvgLineHeight = 0.0;
		pair<int, int> sz = ret.getSize();
		int wid = sz.first, ht = sz.second;
		const int sampleNum = 3;
		double rfPos[sampleNum] = { 0.33, 0.5, 0.67 };
		list<int> lineHeights;  // 记录各行高
		for (int i = 0; i < sampleNum; ++i) {
			int pos = rfPos[i] * wid;
			uchar *col = ret.getColomn(pos);
			int count = 0;  // 行高值计数器
			for (int j = 0; j < ht; ++j) {
				if (col[j] == 0) {
					++count;  // 遇到黑点，累加计数器
				} else {  // 遇到白点
					if (count) {  // 若计数器中有值，则记下该值（一个行高）后计数器归零
						lineHeights.push_back(count);
						count = 0;
					}
				}
			}
		}
		// 由采样得到的各行高计算均值
		if (!lineHeights.empty()) {
			// 滤噪：计算各行高的均值，然后去除各行高中小于均值一半的值和大于均值一倍的值
			newAvgLineHeight = 1.0 * std::accumulate(lineHeights.cbegin(), lineHeights.cend(), 0) / lineHeights.size();
			auto i = lineHeights.begin();
			while (i != lineHeights.end()) {
				if (*i < newAvgLineHeight / 2 || *i > newAvgLineHeight * 2) {
					i = lineHeights.erase(i);
				} else {
					++i;
				}
			}
		}
		if (!lineHeights.empty()) {
			// 重新计算滤噪后的平均行高
			newAvgLineHeight = 1.0 * std::accumulate(lineHeights.cbegin(), lineHeights.cend(), 0) / lineHeights.size();
			if (abs(newAvgLineHeight - avgLineHeight) < elemHeight) {
				nextIter = false;  // 若本次迭代结果与上一次之差小于结构元素的高，则终止迭代
			}
			avgLineHeight = newAvgLineHeight;
			cout << sy << ": " << avgLineHeight << "\n";
		} else {  // 若未发现有效行高，可能发生了错误
			cerr << "Error may have occurred during calculation of the height of lines.";
		}		
	}
	
	//为了方便先将查找文本线写到计算行高一起
	pair<int, int> sz = ret.getSize();
	int wid = sz.first, ht = sz.second;
	const int startpoint = 0.5*wid;   //确定模板搜索起点位置
	uchar *startcol = ret.getColomn(startpoint);  //得到模板搜索起始列的参数
	int  i = 0, CountLine = 0;
	const int Maxline = 50;      //每页最大行数
	Caltextline textline[Maxline];   
	bool LineJudge = true;
	while (LineJudge) {
		if (startcol[i] == 0) {    //检测到新的一行
			list<int> pointX;    //建立记录每点坐标的容器
			list<int> pointY;
			int pointYstart = 0, pointYstart2=0, pointYHeight = 1.4*avgLineHeight;
			int pointXright = startpoint, pointXleft = startpoint;  //向左向右进行模板搜索的起点
			for (int j = i + pointYHeight; j > i; j--) {
				if (startcol[j] == 0) { 
					pointYstart2 = j;
					break; 
				}
			}
			pointYstart = (pointYstart2 + i) / 2;   //得到模板搜索起始列的Y坐标
			pointX.push_back(startpoint);
			pointY.push_back(pointYstart);

			while (true) {  //向左进行模板搜索
				pointXleft -= avgLineHeight;
				if (pointXleft <= 0) break;  //防止越界
				int pointY1 = 0, pointY2 = 0;
				uchar *leftcol = ret.getColomn(pointXleft);
				int draftuppoint = pointYstart - 0.7*avgLineHeight, draftdownpoint = pointYstart + 0.7*avgLineHeight;
				int uppoint = max(0, draftuppoint), downpoint = min(ht, draftdownpoint); //确定每次模板搜索上界与下届，且不越过边界
																						 //分别为从上界开始往下检索与从下界开始网上检索，找到黑色像素即停止
				for (int Y1 = uppoint; Y1 < downpoint; Y1++) {
					if (leftcol[Y1] == 0) {
						pointY1 = Y1;
						break;
					}
				}
				for (int Y2 = downpoint; Y2 > uppoint; Y2--) {
					if (leftcol[Y2] == 0) {
						pointY2 = Y2;
						break;
					}
				}
				pointYstart = (pointY1 + pointY2) / 2;
				pointX.push_back(pointXleft);
				pointY.push_back(pointYstart);

				//判断模板搜索终止条件（到达边界或下一个模板检测不到黑色像素）
				bool judge = false;
				if ((pointXleft - avgLineHeight) > 0) {
					uchar *nextcol = ret.getColomn(pointXleft - avgLineHeight);
					for (int j = uppoint; j < downpoint; j++) {
						if (nextcol[j] == 0) {
							judge = true;
							break;
						}
					}
				} else break;
				if (!judge) break;
			}

			while (true) {  //向右进行模板搜索
				pointXright += avgLineHeight;
				if (pointXright >= wid) break; //防止越界
				int pointY1 = 0, pointY2 = 0;
				uchar *rightcol = ret.getColomn(pointXright);
				int draftuppoint = pointYstart - 0.7*avgLineHeight, draftdownpoint = pointYstart + 0.7*avgLineHeight;
				int uppoint=max(0,draftuppoint), downpoint=min(ht,draftdownpoint); //确定每次模板搜索上界与下届，且不越过图片范围
				//分别为从上界开始往下检索与从下界开始往上检索，找到黑色像素即停止
				for (int Y1 = uppoint; Y1 < downpoint; Y1++) {
					if (rightcol[Y1] == 0) {			
						pointY1 = Y1;
						break;
					}
				}
				for (int Y2 = downpoint; Y2 > uppoint; Y2--) {
					if (rightcol[Y2] == 0) {
						pointY2 = Y2;
						break;
					}
				}
				pointYstart = (pointY1 + pointY2) / 2;
				pointX.push_back(pointXright);
				pointY.push_back(pointYstart);

				//判断模板搜索终止条件（到达边界或下一个模板检测不到黑色像素）
				bool judge = false;
				if ((pointXright + avgLineHeight) < wid) {
					uchar *nextcol = ret.getColomn(pointXright + avgLineHeight);
					for (int j = uppoint; j < downpoint; j++) {
						if (nextcol[j] == 0) {
							judge = true;
							break;
						}
					}
				} else break;
				if (!judge) break;
			}		
			
			//测试某一行检索到的点的代码
			/*
			if (CountLine == 0) {
				cout << endl;
				auto l = pointX.begin();
				while (l != pointX.end()) {
					cout << *l << '\t';
					l++;
				}
				cout << endl;
				auto k = pointY.begin();
				while (k != pointY.end()) {
					cout << *k << '\t';
					k++;
				}
				cout << endl;
			}	
			*/
			textline[CountLine].Fitting(pointX, pointY); //拟合曲线
			cout << "第" << CountLine+1 << "行：" << '\t';
			for (int k = 0; k < FittingN; k++) cout << textline[CountLine].Coefficient[k] << '\t';
			cout << endl;
			CountLine++;   //行数加一
			i += pointYHeight; //跳过目前行的黑点继续往下纵向检索
		}
		else if (i < ht) i++;
	    else LineJudge = false; //纵向检索终止条件
	}
	return avgLineHeight;
}

int Dewarpper::getTextLine(const int &lineHt) {

	return 0;
}

int Dewarpper::reshape() {
	return 0;
}

void Dewarpper::doSave(const PageImage *saveImg, const char *fileName) const {
	/* 私有函数，执行保存图像
	*/
	saveImg->saveImage(fileName);
}

