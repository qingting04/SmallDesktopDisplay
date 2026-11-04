#include "weathernum.h"

#include <TJpg_Decoder.h>

//显示天气图标
//numw 来自 dataSK 的 weathercode（实时天气编码，与预报编码不同）：
//00晴 01多云 02阴 03阵雨 04雷阵雨 05雷阵雨伴冰雹 06雨夹雪 07小雨 08中雨 09大雨
//10暴雨 11大暴雨 12特大暴雨 13阵雪 14小雪 15中雪 16大雪 17暴雪 18雾 19冻雨
//20沙尘暴 21小到中雨 22中到大雨 23大到暴雨 24暴雨到大暴雨 25大暴雨到特大暴雨
//26小到中雪 27中到大雪 28大到暴雪 29浮尘 30扬沙 31强沙尘暴 301雨 302雪 53霾
void WeatherNum::printfweather(int numx,int numy,int numw)
{
  if(numw==0)
  {
    TJpgDec.drawJpg(numx,numy,t0, sizeof(t0));
  }
  else if(numw==1)
  {
    TJpgDec.drawJpg(numx,numy,t1, sizeof(t1));
  }
  else if(numw==2)
  {
    TJpgDec.drawJpg(numx,numy,t3, sizeof(t3));
  }
  else if(numw==3)
  {
    TJpgDec.drawJpg(numx,numy,t4, sizeof(t4));
  }
  else if(numw==4)
  {
    TJpgDec.drawJpg(numx,numy,t5, sizeof(t5));
  }
  else if(numw==5)
  {
    TJpgDec.drawJpg(numx,numy,t11, sizeof(t11));
  }
  else if(numw==6||numw==19)
  {
    TJpgDec.drawJpg(numx,numy,t6, sizeof(t6));
  }
  else if(numw==7||numw==8||numw==21||numw==301)
  {
    TJpgDec.drawJpg(numx,numy,t7, sizeof(t7));
  }
  else if(numw==9||numw==10||numw==11||numw==12||numw==22||numw==23||numw==24||numw==25)
  {
    TJpgDec.drawJpg(numx,numy,t9, sizeof(t9));
  }
  else if(numw==13)
  {
    TJpgDec.drawJpg(numx,numy,t14, sizeof(t14));
  }
  else if(numw==14||numw==15||numw==26||numw==27||numw==302)
  {
    TJpgDec.drawJpg(numx,numy,t15, sizeof(t15));
  }
  else if(numw==16||numw==17||numw==28)
  {
    TJpgDec.drawJpg(numx,numy,t16, sizeof(t16));
  }
  else if(numw==18)
  {
    TJpgDec.drawJpg(numx,numy,t18, sizeof(t18));
  }
  else if(numw==29)
  {
    TJpgDec.drawJpg(numx,numy,t29, sizeof(t29));
  }
  else if(numw==20||numw==30||numw==31)
  {
    TJpgDec.drawJpg(numx,numy,t30, sizeof(t30));
  }
  else if(numw==53||numw==32||numw==49||numw==54||numw==55||numw==56||numw==57||numw==58)
  {
    TJpgDec.drawJpg(numx,numy,t53, sizeof(t53));
  }
  else
  {
    TJpgDec.drawJpg(numx,numy,t99, sizeof(t99));
  }


}
