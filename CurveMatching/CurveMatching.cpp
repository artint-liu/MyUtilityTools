// CurveMatching.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

//#include <iostream>

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

typedef float real_t;
// 定义拟合函数
real_t func(real_t x, real_t a, real_t b, real_t c)
{
    return a * x * x + b * x + c;
}

// 真实函数
real_t real_func(real_t x)
{
    //x = x / (x + 0.155) * 1.019;
    //return pow(x, 2.2f);

    return pow(x, 0.4545f);
}

int main() {

    // 需要拟合的开始结束区间
    const real_t rTestRangeStart = 0.0f;
    const real_t rTestRangeEnd = 1.0f;
    const int nTestStep = 100;

    // 生成示例数据
    real_t xdata[nTestStep + 1];
    real_t ydata[nTestStep + 1];
    real_t step = (rTestRangeEnd - rTestRangeStart) / (nTestStep);
    for (int i = 0; i <= nTestStep; i++) {
        xdata[i] = rTestRangeStart + i * step;
        ydata[i] = real_func(xdata[i]);
    }

    // 拟合参数初始化
    real_t a = 1.0f, b = 1.0f, c = 1.0f;
    real_t sumError = 0.0;
    real_t bestError = 1e10;
    real_t bestA, bestB, bestC;

    real_t centA = 0;
    real_t centB = 0;
    real_t centC = 0;
    real_t offsetA = 100.f;
    real_t offsetB = 100.f;
    real_t offsetC = 100.f;

    real_t stride = 1.f;

    // 尝试不同的参数组合进行拟合
    do {
        for (a = centA - offsetA; a <= centA + offsetA; a += stride) {
            for (b = centB - offsetA; b <= centB + offsetB; b += stride) {
                for (c = centC - offsetC; c <= centC + offsetC; c += stride) {
                    sumError = 0.0;
                    for (int i = 0; i <= nTestStep; i++) {
                        real_t yPred = func(xdata[i], a, b, c);
                        real_t error = ydata[i] - yPred;
                        sumError += error * error;
                    }
                    if (sumError < bestError) {
                        bestError = sumError;
                        bestA = a;
                        bestB = b;
                        bestC = c;
                    }
                }
            }
        }
        centA = bestA;
        centB = bestB;
        centC = bestC;
        offsetA = stride;
        offsetB = stride;
        offsetC = stride;
        stride *= 0.5;
    } while (stride > 1e-5f);

    // 打印拟合参数
    printf("拟合参数: a = %lf, b = %lf, c = %lf\n", bestA, bestB, bestC);
    printf("x * x * %lf + x * %lf + %lf\n", bestA, bestB, bestC);

    return 0;
}