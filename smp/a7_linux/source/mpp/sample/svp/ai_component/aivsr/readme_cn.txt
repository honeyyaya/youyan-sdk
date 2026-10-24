编译说明：
1. 在 <sdk 目录>/smp/a7_linux/source/mpp/sample/svp/ai_component/aivsr 目录执行make命令即可

使用说明：
1. 在运行 sample 前设置 LD_LIBRARY_PATH：
    export SDK_PATH=<sdk 目录>
    export LD_LIBRARY_PATH=$SDK_PATH/smp/a7_linux/source/out/lib:$LD_LIBRARY_PATH

2. 运行 sample：
    ./sample_aivsr -c <用例index> -p <输入nv21图片路径> -s <输入图片尺寸，宽x高> -v <无入参,打开从路径读取模型>

    测试的模型文件在$SDK_PATH/smp/a7_linux/source/out/lib/model/aivsr

    e.g.
        ./sample_aivsr -c 0 -p /xxx/xxx/xxx.yuv -s 1920x1080
        ./sample_aivsr -c 1 -p /xxx/xxx/xxx.yuv -s 1920x1080
        ./sample_aivsr -c 2 -p /xxx/xxx/xxx.yuv -s 1920x1080 -n name -o ./database/name.txt
        ./sample_aivsr -c 3 -p /xxx/xxx/xxx.yuv -s 1920x1080 -d ./database
    注：内存资源紧张时，推荐使用路径传入的方式，减少创建多个通路时的内存峰值

3. 参数：
    -c: 指定运行的sample
            0: 人脸识别(face_capture)
            1: 车牌识别(vlpr)
            2: 人脸底库特征提取(fr)
            3: 人脸特征与底库比对(fr)
    -v: 指定创建通道是通过传入路径还是内存，默认为内存传入
    -p: 输入图片路径，输入图片需为nv21格式的yuv图片
    -s: 输入图片尺寸，宽x高
    -n: 输入底库图片名称或id
    -o：底库图片特征信息的输出文件路径
    -d: 输入底库特征信息文件的文件夹路径，支持路径下有多个底库特征文件
    -h: 显示参数