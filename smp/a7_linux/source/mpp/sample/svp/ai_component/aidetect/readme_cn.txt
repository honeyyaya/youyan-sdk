编译说明：
1. 在 <sdk 目录>/smp/a7_linux/source/mpp/sample/svp/ai_component/aidetect 目录执行make命令即可

使用说明：
1. 在运行 sample 前设置 LD_LIBRARY_PATH：
    export SDK_PATH=<sdk 目录>
    export LD_LIBRARY_PATH=$SDK_PATH/smp/a7_linux/source/out/lib:$LD_LIBRARY_PATH

2. 运行 sample：
    ./sample_aidetect -i <yuv格式图片> -m <模型bin文件> -s <width x height> -v <无入参,打开从路径读取模型>

    测试的模型文件在$SDK_PATH/smp/a7_linux/source/out/lib/model/aidetect
    测试的输入图片在aidetect目录下的data中
    e.g.
        ./sample_aidetect -i ./data/hvf_image_hor_1920x1080.yuv -m ../../../../../out/model/aidetect/det_hvf_hor.bin -s 1920x1080
        ./sample_aidetect -i ./data/hvf_image_ver_1080x1920.yuv -m ../../../../../out/model/aidetect/det_hvf_ver.bin -s 1080x1920 -v

    注：内存资源紧张时，推荐使用路径传入的方式，减少创建多个通路时的内存峰值

3. 参数：
    -i: 指定测试图片路径
    -s: 指定测试图片的大小，用x分割，比如1920x1080
    -m: 指定测试的模型路径
    -v: 指定创建通道是通过传入路径还是内存，默认为内存传入
    -h: 显示参数