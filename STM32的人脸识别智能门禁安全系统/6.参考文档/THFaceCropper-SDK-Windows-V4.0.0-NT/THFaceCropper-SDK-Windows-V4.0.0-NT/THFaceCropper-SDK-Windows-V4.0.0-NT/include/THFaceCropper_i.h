#ifndef THFACECROPPER_I_H
#define THFACECROPPER_I_H

#ifdef _MSC_VER
#   ifdef __cplusplus
#       ifdef THFACECROPPER_STATIC_LIB
#           define THFACECROPPER_API  extern "C"
#       else
#           ifdef SDK_EXPORTS
#               define THFACECROPPER_API extern "C" __declspec(dllexport)
#           else
#               define THFACECROPPER_API extern "C" __declspec(dllimport)
#           endif
#       endif
#   else
#       ifdef THFACECROPPER_STATIC_LIB
#           define THFACECROPPER_API
#       else
#           ifdef SDK_EXPORTS
#               define THFACECROPPER_API __declspec(dllexport)
#           else
#               define THFACECROPPER_API __declspec(dllimport)
#           endif
#       endif
#   endif
#else /* _MSC_VER */
#   ifdef __cplusplus
#       ifdef SDK_EXPORTS
#           define THFACECROPPER_API extern "C" __attribute__((visibility ("default")))
#       else
#           define THFACECROPPER_API extern "C"
#       endif
#   else
#       ifdef SDK_EXPORTS
#           define THFACECROPPER_API __attribute__((visibility ("default")))
#       else
#           define THFACECROPPER_API
#       endif
#   endif
#endif

THFACECROPPER_API int				THFaceCropper_Init();
/*
 描述：SDK初始化
 参数：
	无
 返回值：
	0,初始化SDK成功
	-1,初始化SDK失败

*/

THFACECROPPER_API void				THFaceCropper_Uninit();
/*
 描述：SDK释放
 参数：
	无
 返回值：
	无
*/

THFACECROPPER_API	void			THFaceCropper_Memory_Free(void* memory);
/*
 描述：释放SDK内部分配的内存，如：THFaceCropper_Execute1或THFaceCropper_Execute2等API内部分配的内存
 参数：
	memory,内存地址
 返回值：
	无
*/

THFACECROPPER_API unsigned char* 	THFaceCropper_Execute1(const char* img_file_name,int* out_size,int* error_code);
/*
 描述：根据图像文件名进行人脸抠图
 参数：
	img_file_name[input],待加载的图像文件名
	out_size[output],输出数据的字节数
	error_code[output],错误码:
		-1，加载图像文件失败
		-2,人脸检测失败
		-3，人脸抠图失败
		-4，图像数据编码失败
		-101，人脸太小
		-102，人脸关键点定位不准确
		-103，人脸姿态角度yaw偏大
		-104，人脸姿态角度pitch偏大
		-105，人脸姿态角度roll偏大
		-111，人脸太暗
		-112，人脸太亮
 返回值：
	NULL,切图失败
	非0，输出数据的数据的内存地址，该内存由调用方释放(THFaceCropper_Memory_Free)，数据大小为*out_size
*/

THFACECROPPER_API unsigned char* 	THFaceCropper_Execute2(const unsigned char* img_file_data,int img_file_size,int* out_size,int* error_code);
/*
 描述：根据图像文件数据进行人脸抠图
 参数：
	img_file_data[input],图像文件数据
	img_file_size[input],图像文件数据大小
	out_size[output],输出数据的字节数
	error_code[output],错误码：
		-1，加载图像文件失败
		-2,人脸检测失败
		-3，人脸抠图失败
		-4，图像数据编码失败
		-101，人脸太小
		-102，人脸关键点定位不准确
		-103，人脸姿态角度yaw偏大
		-104，人脸姿态角度pitch偏大
		-105，人脸姿态角度roll偏大
		-111，人脸太暗
		-112，人脸太亮
 返回值：
	NULL,切图失败
	非0，输出数据的数据的内存地址，该内存由调用方释放(THFaceCropper_Memory_Free)，数据大小为*out_size
*/

THFACECROPPER_API void THFaceCropper_SetFaceCheck(int nCheckModel = 0);
/*
 描述：设置是否执行人脸质量校验
 参数：
	nCheckModel,0-不执行质量校验 1-执行人脸质量校验
 返回值：
	无
*/

THFACECROPPER_API unsigned char* THFaceCropper_Execute0(const char* img_file_name, const char* out_format, int* out_size, int* error_code);
/*
 描述：根据图像文件名进行人脸抠图
 参数：
	img_file_name[input],待加载的图像文件名
	out_format[input],输出的图像编码格式,如:".jpg",".png",".bmp"
	out_size[output],输出数据的字节数
	error_code[output],错误码:
		-1，加载图像文件失败
		-2,人脸检测失败
		-3，人脸抠图失败
		-4，图像数据编码失败
		-101，人脸太小
		-102，人脸关键点定位不准确
		-103，人脸姿态角度yaw偏大
		-104，人脸姿态角度pitch偏大
		-105，人脸姿态角度roll偏大
		-111，人脸太暗
		-112，人脸太亮
 返回值：
	NULL,抠图失败
	非0，输出图像的数据的内存地址，该内存由调用方释放(THFaceCropper_Memory_Free)，数据大小为*out_size
*/


THFACECROPPER_API unsigned char* THFaceCropper_Feature(const char* img_file_name, int* out_size, int* error_code);
/*
 描述：根据图像文件名进行人脸特征提取
 参数：
	img_file_name[input],待加载的图像文件名
	out_size[output],输出特征数据的字节数
	error_code[output],错误码:
		-1，加载图像文件失败
		-2,人脸检测失败
		-3，人脸抠图失败
		-4，提取人脸特征失败
		-101，人脸太小
		-102，人脸关键点定位不准确
		-103，人脸姿态角度yaw偏大
		-104，人脸姿态角度pitch偏大
		-105，人脸姿态角度roll偏大
		-111，人脸太暗
		-112，人脸太亮
 返回值：
	NULL,人脸特征提取失败
	非0，输出图像的数据的内存地址，该内存由调用方释放(THFaceCropper_Memory_Free)，数据大小为*out_size
*/

#endif