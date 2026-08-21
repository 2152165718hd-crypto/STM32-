【头文件】

[include]
1.THFaceCropper_i.h 接口文件

【链接库】
[lib]
lib/x86或lib/x64目录下的THFaceCropper.lib

【运行库】

[bin]
接口库文件
1.bin/x86或bin/x64目录下的THFaceCropper.dll

依赖库文件
bin/x86或bin/x64目录下的其他dll文件和feadb.db80

【测试工程】
[test]
示例工程

运行test:
test mm.jpg mm.bin 1 5
对mm.jpg进行API调用测试,保存切图数据到mm.bin
1-->启用人脸质量校验
5-->重复运行5次

[test-feature]
测试THFaceCropper_Feature接口示例工程

运行test:
test mm.jpg mm.fea 1 5
对mm.jpg进行THFaceCropper_Feature调用测试,保存特征数据到mm.fea
1-->启用人脸质量校验
5-->重复运行5次

【接口说明】

参考THFaceCropper_i.h的注释

