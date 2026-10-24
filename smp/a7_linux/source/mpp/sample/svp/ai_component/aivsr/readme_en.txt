Compile Instructions:
1. In <sdk path>/smp/a7_linux/source/mpp/sample/svp/ai_component/aivsr , run command "make" to compile

Execute Instructions:
1. Before run the sample, set the LD_LIBRARY_PATH:
    export SDK_PATH=<sdk path>
    export LD_LIBRARY_PATH=$SDK_PATH/smp/a7_linux/source/out/lib:$LD_LIBRARY_PATH

2. Run the sample:
    ./sample_aivsr -c <case index> -p <input nv21 image path> -s <input image size, width x height> -v <not input, add to read model from path>

    The model file path is in $SDK_PATH/smp/a7_linux/source/out/lib/model/aivsr

    e.g.
        ./sample_aivsr -c 0 -p /xxx/xxx/xxx.yuv -s 1920x1080
        ./sample_aivsr -c 1 -p /xxx/xxx/xxx.yuv -s 1920x1080
        ./sample_aivsr -c 2 -p /xxx/xxx/xxx.yuv -s 1920x1080 -n name -o ./database/name.txt
        ./sample_aivsr -c 3 -p /xxx/xxx/xxx.yuv -s 1920x1080 -d ./database
    HINT: In case of memory shortage, reading from path is recommended to minimize the peak of the memory usage

3. parameters:
    -c: Specify the case index
        0: face capture
        1: vehicle license plate recognize(vlpr)
        2: face database feature extraction (fr)
        3: comparison of facial features and database (fr)
    -v: Specify if the channel is created through a path or memory, default by reading from memory
    -p: Input image path. The input image must be a YUV image in nv21 format.
    -s: Input image size (width x height)
    -n: Enter the name or ID of the database image.
    -o: Output file path of the database image feature information.
    -d: Enter the directory path of the database feature file. Multiple database feature files can be stored in this directory.
    -h: Show parameters info