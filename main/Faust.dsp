import("stdfaust.lib");
ctFreq = hslider("cutoffFrequency",500,50,3000,0.01);
res = hslider("res",0.5,0,1,0.1);
gain = hslider("gain",1,0,1,0.01);
process = no.pink_noise : ve.moog_vcf(res,ctFreq) * gain;
