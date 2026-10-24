Sample Description:
1. The data path of sample_aidetect_vie is as follows.
    vi-vpss-venc                chn_0
        └──aidetect-vgs-venc    chn_1
    1) stream save path: vi-vpss-venc
    2) AI service path: vi-vpss-aidetect-vgs-venc
2. The sensor used in sample_aidetect_vie is same as other SDK sample.
   Please modify it in the <sdk_path>/smp/a7_linux/source/mpp/sample/Makefile.param.

Compile Instructions:
1. In <sdk_path>/smp/a7_linux/source/mpp/sample/svp/ai_component/aidetect_vie , run command "make" to compile

Execute Instructions:
1. Before run the sample, set the LD_LIBRARY_PATH:
    export SDK_PATH=<sdk_path>
    export LD_LIBRARY_PATH=$SDK_PATH/smp/a7_linux/source/out/lib:$LD_LIBRARY_PATH

2. Run the sample:
    ./sample_aidetect_vie <model bin file>
    The model file path is in $SDK_PATH/smp/a7_linux/source/out/lib/model/aidetect

3. Parameters:
    -h: Show help info