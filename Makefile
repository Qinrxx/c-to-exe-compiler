CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall -static

mycc: compiler.cpp
	$(CXX) $(CXXFLAGS) -o mycc compiler.cpp

clean:
	rm -f mycc *.s test1.exe test2.exe test3.exe
