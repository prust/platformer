platformermake:
ifeq ($(OS),Windows_NT)
	gcc -o platformer.exe platformer.c -I sdl-win/include/SDL2 -L sdl-win/lib -lmingw32 -lSDL2main -lSDL2
else
	gcc -o platformer platformer.c SDL2_gfxPrimitives.c SDL2_rotozoom.c -L/usr/local/lib -Iinclude -F/Library/Frameworks -framework SDL2
endif

platformerdebug:
	gcc -g -o platformer platformer.c -L/usr/local/lib -Iinclude -F/Library/Frameworks -framework SDL2
