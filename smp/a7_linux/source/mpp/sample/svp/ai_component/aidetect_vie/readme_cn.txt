sample说明：
1. sample_aidetect_vie的数据通路如下。
    vi-vpss-venc                chn_0
        └──aidetect-vgs-venc    chn_1
    1) 码流录制通路：vi-vpss-venc
    2) AI业务通路：vi-vpss-aidetect-vgs-venc
2. sample_aidetect_vie使用的sensor和sdk sample一致，请在<sdk 目录>/smp/a7_linux/source/mpp/sample/Makefile.param中修改。

编译说明：
1. 在 <sdk 目录>/smp/a7_linux/source/mpp/sample/svp/ai_component/aidetect_vie 目录执行make命令即可

使用说明：
1. 在运行 sample 前设置 LD_LIBRARY_PATH：
    export SDK_PATH=<sdk 目录>
    export LD_LIBRARY_PATH=$SDK_PATH/smp/a7_linux/source/out/lib:$LD_LIBRARY_PATH

2. 运行 sample：
    ./sample_aidetect_vie <模型bin文件>
    测试的模型文件在$SDK_PATH/smp/a7_linux/source/out/lib/model/aidetect

3. 参数：
    -h: 显示帮助信息