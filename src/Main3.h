#include "/home/codeleaded/System/Static/Library/WindowEngine.h"
#define FONT_PATH "./assets/JetBrainsMono-Bold.ttf"

//AlxFont f_blocky;
//AlxFont f_high;
//AlxFont f_yanis;



void Setup(AlxWindow* w){
	//f_blocky = AlxFont_Make(Sprite_Load("./assets/Alx_Font_Blocky.png"),16,16,8,8,64,64);
	//f_high = AlxFont_Make(Sprite_Load("./assets/Alx_Font_High.png"),16,16,32,64,32,64);
	//f_yanis = AlxFont_Make(Sprite_Load("./assets/Alx_Font_Yanis.png"),16,16,16,16,64,64);
}
void Update(AlxWindow* w){
	Clear(BLACK);

    //CStr_RenderAlxFontf(WINDOW_STD_ARGS,&f_blocky,  0.0f,0.0f,WHITE,"Hello World!");
    //CStr_RenderAlxFontf(WINDOW_STD_ARGS,&f_high,    0.0f,f_blocky.CharSizeY,WHITE,"Hello World!");
    //CStr_RenderAlxFontf(WINDOW_STD_ARGS,&f_yanis,   0.0f,f_blocky.CharSizeY + f_high.CharSizeY,WHITE,"Hello World!");
}
void Delete(AlxWindow* w){
	//AlxFont_Free(&f_blocky);
	//AlxFont_Free(&f_high);
	//AlxFont_Free(&f_yanis);
}

int main(){
    if(Create("Fonts",1200,1200,1,1,Setup,Update,Delete))
        Start();
    return 0;
}