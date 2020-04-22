all:
	g++ -Wall -pthread -O0 -std=c++17 at.cpp -o at
clean:
	rm at
