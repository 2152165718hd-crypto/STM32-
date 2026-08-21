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

static int sim_crypt_data(unsigned char* data, int size)
{
	if (data == NULL || size <= 0) return -1;

	const int PKG_KEY_SIZE = 8;
	const unsigned char PKG_KEY[PKG_KEY_SIZE] = { 0xa4, 0x6d, 0xfe, 0xd5, 0x7b, 0x9e, 0x3c, 0xdf };

	for (int i = 0; i < size; i++)
	{
		data[i] ^= PKG_KEY[i % PKG_KEY_SIZE];
	}

	return 0;
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
		int size = 0;
		int err_code;

		t1 = msecond();
		unsigned char* pData = THFaceCropper_Execute1(sFile, &size, &err_code);
		t2 = msecond();

		//printf("THFaceCropper_Execute1 time=%f，size=%d\n", t2 - t1,size);

		if (err_code != 0)
		{
			printf("THFaceCropper_Execute1 failed.error code=%d\n", err_code);
			break;
		}

		FILE* f = fopen(sFile_dst, "wb");
		if (f)
		{
			fwrite(pData, 1, size, f);
			fclose(f);
		}

#if 1
		sim_crypt_data(pData, size);
		FILE* fjpg = fopen("crop120.jpg", "wb");
		if (fjpg)
		{
			fwrite(pData, 1, size, fjpg);
			fclose(fjpg);
		}
#endif 

		//调用方释放内存
		THFaceCropper_Memory_Free(pData);


#if 0
		int out_size = 0;
		unsigned char* pData320 = THFaceCropper_Execute0(sFile, ".jpg", &out_size, &err_code);
		if (err_code == 0)//success
		{

			FILE* f = fopen("crop320.jpg", "wb");
			if (f)
			{
				fwrite(pData320, 1, out_size, f);
				fclose(f);
			}

			THFaceCropper_Memory_Free(pData320);
		}
		else
		{
			printf("THFaceCropper_Execute0 failed.error code=%d\n", err_code);
		}
#endif
		
		
//		printf("********************************************************** %d end\n", index++);
	}

	//反初始化SDK
	THFaceCropper_Uninit();

//	printf("Press any key to exit\n");
//	getchar();

	return 0;
}



