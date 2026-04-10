CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall -static

mycc.exe: compiler.cpp
	$(CXX) $(CXXFLAGS) -o mycc.exe compiler.cpp

test: mycc.exe
	./run_tests.bat

clean:
	rm -f mycc.exe *.s test1.exe test2.exe test3.exe test4.exe test5.exe test6.exe test7.exe test8.exe test9.exe test10.exe
