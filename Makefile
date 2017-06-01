Shell: Shell.o
	g++ Shell.o -o Shell

Shell.o: Shell.cpp
	g++ Shell.cpp -c