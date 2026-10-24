Compile Instructions:
1. In <sdk path>/smp/a7_linux/source/mpp/sample/svp/ai_component/aidetect , run command "make" to compile

Execute Instructions:
1. Before run the sample, set the LD_LIBRARY_PATH:
    export SDK_PATH=<sdk path>
    export LD_LIBRARY_PATH=$SDK_PATH/smp/a7_linux/source/out/lib:$LD_LIBRARY_PATH

2. Run the sample:
    ./sample_aidetect -i <yuv format image> -m <model bin file> -s <width x height> -v <no input, add to read model from path>

    The model file path is in $SDK_PATH/smp/a7_linux/source/out/lib/model/aidetect
    The input image file is in ./data
    e.g.
        ./sample_aidetect -i ./data/hvf_image_hor_1920x1080.yuv -m ../../../../../out/model/aidetect/det_hvf_hor.bin -s 1920x1080
        ./sample_aidetect -i ./data/hvf_image_ver_1080x1920.yuv -m ../../../../../out/model/aidetect/det_hvf_ver.bin -s 1080x1920 -v

    HINT: In case of memory shortage, reading from path is recommended to reduced the peak of the memory usage

3. Parameters:
    -i: Specify the test image
    -s: Specify the test image size, seperated by 'x', like 1920x1080
    -m: Specify the test model
    -v: Specify if the channel is created through a path or memory, default by reading from memory
    -h: Show parameters info