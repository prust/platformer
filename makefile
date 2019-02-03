platformermake:
ifeq ($(OS),Windows_NT)
	gcc -o platformer.exe platformer.c -I /c/msys64/usr/lib/sdl2/x86_64-w64-mingw32/include/SDL2 -L /c/msys64/usr/lib/sdl2/x86_64-w64-mingw32/lib -lmingw32 -lSDL2main -lSDL2
else
	gcc -o platformer platformer.c -L/usr/local/lib -I/Library/Frameworks/SDL2.framework/Headers -I/Library/Frameworks/SDL2_image.framework/Headers -I/Library/Frameworks/SDL2_mixer.framework/Headers -F/Library/Frameworks -framework SDL2
endif

platformerdebug:
	gcc -g -o platformer platformer.c -L/usr/local/lib -I/Library/Frameworks/SDL2.framework/Headers -I/Library/Frameworks/SDL2_image.framework/Headers -I/Library/Frameworks/SDL2_mixer.framework/Headers -F/Library/Frameworks -framework SDL2
