Sample Description:
1. This sample uses vlpr task as an example to display how to use async process interface of aivsr.
2. The data path of sample_aivsr_vie is as follows.
    vi-vpss-venc        chn_0
        └──aivsr-venc   chn_1
    1) stream save path: vi-vpss-venc.
    2) AI service path: vi-vpss-aivsr-venc.
3. The sensor used in sample_aivsr_vie is same as other SDK sample.
   Please modify it in the <sdk_path>/smp/a7_linux/source/mpp/sample/Makefile.param.

Compile Instructions:
1. In <sdk_path>/smp/a7_linux/source/mpp/sample/svp/ai_component/aivsr_vie , run command "make" to compile

Execute Instructions:
1. Before run the sample, set the LD_LIBRARY_PATH:
    export SDK_PATH=<sdk_path>
    export LD_LIBRARY_PATH=$SDK_PATH/smp/a7_linux/source/out/lib:$LD_LIBRARY_PATH

2. Run the sample:
    ./sample_aivsr_vie <detect model bin file> <vlpr model bin file>
    The model file path is in $SDK_PATH/smp/a7_linux/source/out/lib/model
    vsr_vlpr.bin：Run this model on a chip platform with more than 256MB
    vsr_vlpr_lite.bin：Run this model on a chip platform with more than 128MB

3. Parameters:
    -h: Show help info