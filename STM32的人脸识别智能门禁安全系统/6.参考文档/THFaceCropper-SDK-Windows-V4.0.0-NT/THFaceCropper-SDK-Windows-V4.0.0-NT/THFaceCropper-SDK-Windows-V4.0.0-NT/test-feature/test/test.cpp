// test.cpp : 定义控制台应用程序的入口点。
//

#include "stdafx.h"
#include <Windows.h>

#include "../../include/THFaceCropper_i.h"

#ifndef _WIN64 //win32
#pragma comment(lib,"../../lib/x86/THFaceCropper.lib")
#else
#pragma comment(lib,"../../lib/x64/THFaceCropper.lib")
#endif


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <string>
using namespace std;



#include <time.h>
//get current system time
int gettimeofday(struct timeval *tp, void *tzp)
{
	time_t clock;
	struct tm tm;
	SYSTEMTIME wtm;
	GetLocalTime(&wtm);
	tm.tm_year = wtm.wYear - 1900;
	tm.tm_mon = wtm.wMonth - 1;
	tm.tm_mday = wtm.wDay;
	tm.tm_hour = wtm.wHour;
	tm.tm_min = wtm.wMinute;
	tm.tm_sec = wtm.wSecond;
	tm.tm_isdst = -1;
	clock = mktime(&tm);
	tp->tv_sec = clock;
	tp->tv_usec = wtm.wMilliseconds * 1000;
	return (0);
}
double msecond()
{
	struct timeval tv;
	gettimeofday(&tv, 0);
	return (tv.tv_sec * 1.0e3 + tv.tv_usec * 1.0e-3);
}

int main(int argc, char **argv)
{

	if (argc < 4)
	{
		printf("Parameter error.\n");
		return 0;
	}

	char* sFile = argv[1];
	char* sFile_dst = argv[2];
	int nChekFace = atoi(argv[3]);

	//循环执行的次数
	int nTimes = 1;
	if (argv[4])
	{
		nTimes = atoi(argv[4]);
	}

	//初始化SDK
	int ret = THFaceCropper_Init();
	if (ret != 0)
	{
		printf("THFaceCropper_Init failed.ret=%d\n", ret);
		return 0;
	}

	THFaceCropper_SetFaceCheck(nChekFace);

	double t1, t2;



//	printf("**********************************************************\n");
	int index = 1;
	while (nTimes-->0)//根据第二个参数决定重复执行次数
	{
		int fea_size = 0;
		int err_code;

		t1 = msecond();
		unsigned char* pFeature = THFaceCropper_Feature(sFile, &fea_size, &err_code);
		t2 = msecond();

		printf("THFaceCropper_Execute1 time=%f，size=%d\n", t2 - t1, fea_size);

		if (err_code != 0)
		{
			printf("THFaceCropper_Execute1 failed.error code=%d\n", err_code);
			break;
		}

		FILE* f = fopen(sFile_dst, "wb");
		if (f)
		{
			fwrite(pFeature, 1, fea_size, f);
			fclose(f);
		}



		//调用方释放内存
		THFaceCropper_Memory_Free(pFeature);
		
//		printf("********************************************************** %d end\n", index++);
	}

	//反初始化SDK
	THFaceCropper_Uninit();

//	printf("Press any key to exit\n");
//	getchar();

	return 0;
}



