PROG    := BasicoOpenGL.exe
SRC     := Ponto.cpp Poligono.cpp Temporizador.cpp ListaDeCoresRGB.cpp Instancia.cpp ModeloMatricial.cpp TransformacoesGeometricas.cpp
OBJS    := $(SRC:.cpp=.o)

CXX     := g++
CXXFLAGS+= -std=gnu++14 -O2 -Wall -Wextra -DNOMINMAX -Iinclude/GL

# sem -Llib (suas libs lá eram 32-bit). use as do MSYS2:
LDFLAGS :=
LDLIBS  := -lfreeglut -lopengl32 -lglu32 -lwinmm -lgdi32 -limm32

$(PROG): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(LDFLAGS) $(LDLIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

.PHONY: clean
clean:
	-@if exist *.o del /q /f *.o
	-@if exist $(PROG) del /q /f $(PROG)
