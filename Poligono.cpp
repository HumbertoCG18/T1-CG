//
//  Poligono.cpp
//  OpenGLTest
//
//  Created by Márcio Sarroglia Pinho on 18/08/20.
//  Copyright © 2020 Márcio Sarroglia Pinho. All rights reserved.
//
#include <iostream>
#include <fstream>
using namespace std;

#include "Poligono.h"
#include <GL/gl.h>

Poligono::Poligono()
{
}

void Poligono::insereVertice(Ponto p)
{
    Vertices.push_back(p);
}

void Poligono::insereVertice(Ponto P, int pos)
{
    // Normaliza pos para o intervalo [0, Vertices.size()]
    if (pos < 0) pos = 0;
    if (static_cast<size_t>(pos) > Vertices.size())
        pos = static_cast<int>(Vertices.size());

    Vertices.insert(Vertices.begin() + pos, P);
}

Ponto Poligono::getVertice(int i)
{
    return Vertices[static_cast<size_t>(i)];
}

void Poligono::pintaPoligono()
{
    glBegin(GL_POLYGON);
    for (size_t i = 0; i < Vertices.size(); ++i)
        glVertex3f(Vertices[i].x, Vertices[i].y, Vertices[i].z);
    glEnd();
}

void Poligono::desenhaPoligono()
{
    glBegin(GL_LINE_LOOP);
    for (size_t i = 0; i < Vertices.size(); ++i)
        glVertex3f(Vertices[i].x, Vertices[i].y, Vertices[i].z);
    glEnd();
}

void Poligono::desenhaVertices()
{
    glBegin(GL_POINTS);
    for (size_t i = 0; i < Vertices.size(); ++i)
        glVertex3f(Vertices[i].x, Vertices[i].y, Vertices[i].z);
    glEnd();
}

void Poligono::imprime()
{
    for (size_t i = 0; i < Vertices.size(); ++i)
        Vertices[i].imprime();
}

unsigned long Poligono::getNVertices()
{
    return static_cast<unsigned long>(Vertices.size());
}

void Poligono::obtemLimites(Ponto &Min, Ponto &Max)
{
    Max = Min = Vertices[0];

    for (size_t i = 0; i < Vertices.size(); ++i)
    {
        Min = ObtemMinimo(Vertices[i], Min);
        Max = ObtemMaximo(Vertices[i], Max);
    }
}

// **********************************************************************
//
// **********************************************************************
void Poligono::LePoligono(const char *nome)
{
    ifstream input;            // ofstream arq;
    input.open(nome, ios::in); // arq.open(nome, ios::out);
    if (!input)
    {
        cout << "Erro ao abrir " << nome << ". " << endl;
        exit(0);
    }
    cout << "Lendo arquivo " << nome << "...";
    string S;
    // int nLinha = 0;
    unsigned int qtdVertices;

    input >> qtdVertices; // arq << qtdVertices

    for (unsigned int i = 0; i < qtdVertices; ++i)
    {
        double x, y;
        // Le cada elemento da linha
        input >> x >> y; // arq << x  << " " << y << endl
        if (!input)
            break;
        // nLinha++;
        insereVertice(Ponto(x, y));
    }
    cout << "Poligono lido com sucesso!" << endl;
}

void Poligono::getAresta(int n, Ponto &P1, Ponto &P2)
{
    // Assume 0 <= n < Vertices.size()
    const size_t idx = static_cast<size_t>(n);
    P1 = Vertices[idx];
    const size_t n1 = (idx + 1) % Vertices.size();
    P2 = Vertices[n1];
}

void Poligono::desenhaAresta(int n)
{
    const size_t idx = static_cast<size_t>(n);
    glBegin(GL_LINES);
        glVertex3f(Vertices[idx].x, Vertices[idx].y, Vertices[idx].z);
        const size_t n1 = (idx + 1) % Vertices.size();
        glVertex3f(Vertices[n1].x, Vertices[n1].y, Vertices[n1].z);
    glEnd();
}

void Poligono::alteraVertice(int i, Ponto P)
{
    Vertices[static_cast<size_t>(i)] = P;
}
