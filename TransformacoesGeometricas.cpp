// **********************************************************************
// PUCRS/Escola Politecnica
// COMPUTACAO GRAFICA - Trabalho 1 (2025/2)
// Autor: Humberto (adaptação/integração orientada)
// **********************************************************************
// Requisitos implementados:
// - Disparador controlado por setas (rotaciona L/R, acelera Up, desacelera Down)
// - Disparador respeita limites laterais (e verticais) da janela
// - Tiros do jogador (ESPAÇO) com até 20 simultâneos
// - Naves inimigas (>= 4 modelos) com movimento automático dentro da tela
// - Tiros automáticos aleatórios e independentes dos inimigos
// - Colisão via OOBB (envelope) entre tiros e naves / jogador
// - Fim de jogo: vitória (todas as naves destruídas) ou derrota (vidas = 0)
// - Modelos lidos de arquivo no formato matricial (já usado no projeto base)
// **********************************************************************

#include <iostream>
#include <cmath>
#include <ctime>
#include <fstream>
#include <vector>
#include <random>
#include <algorithm>

using namespace std;

#ifdef WIN32
  // Não inclua <windows.h> aqui para evitar conflitos de macros.
  // freeglut/glut lidam com isso.
  #include <glut.h>
#else
  #include <sys/time.h>
#endif

#ifdef __APPLE__
  #include <GLUT/glut.h>
#endif

#ifdef __linux__
  #include <GL/glut.h>
#endif

#include "Ponto.h"
#include "Poligono.h"
#include "Instancia.h"
#include "ModeloMatricial.h"
#include "Temporizador.h"
#include "ListaDeCoresRGB.h"
// Em alguns templates do prof. há um Linha.h; não é obrigatório aqui.
// #include "Linha.h"

// ---------------------------------------------------------------------
// Globais
// ---------------------------------------------------------------------
Temporizador T;              // animação (redesenho)
Temporizador Tick;           // lógica (movimento/IA)
double AccumDeltaT = 0.0;
double AccumLogic  = 0.0;

Poligono Mapa, MeiaSeta, Mastro;

constexpr int MAX_INSTANCIAS = 500;
constexpr int AREA_DE_BACKUP = 250;    // reserva para backups de spawn/respawn
Instancia Personagens[MAX_INSTANCIAS + AREA_DE_BACKUP];

ModeloMatricial Modelos[32];
int nInstancias   = 0;  // instâncias ativas (0 = jogador; demais = inimigos/tiros)
int nModelos      = 0;  // quantos modelos carregados no vetor Modelos

// Índices fixos de modelos
int ID_MODELO_JOGADOR   = 0;
int ID_MODELO_PROJETIL  = 1;
int ID_MODELO_INICIO_NAVES = 2; // a partir daqui, naves inimigas

// Parâmetros de jogo
constexpr int   MAX_TIROS_JOGADOR = 20;
constexpr int   MAX_TIROS_INIMIGOS = 100;
int Vidas = 3;

// “Tags” para identificar de quem é o tiro (usamos Pivot.z como campo livre)
enum DonoDoTiro { OWNER_NINGUEM = 0, OWNER_JOGADOR = 1, OWNER_INIMIGO = 2 };

// Limites lógicos da área de desenho
Ponto Min, Max;

// Controle de desenho do envelope
bool desenhaEnvelope = false;

// Aleatoriedade
std::mt19937_64 rng(123456); // semente fixa (determinístico). Troque para time(NULL) se quiser.
std::uniform_real_distribution<float> u01(0.0f, 1.0f);

// Estado auxiliar
int PersonagemAtual = 0; // quem está sendo desenhado (para DesenhaPersonagemMatricial)

float angulo = 0.0f; // para os elementos decorativos (catavento, se usados)

// ---------------------------------------------------------------------
// Utilidades
// ---------------------------------------------------------------------
static inline float clampf(float v, float a, float b) {
    return std::max(a, std::min(v, b));
}

// Evita sair da tela (usado em jogador e inimigos)
void MantemDentroDosLimites(int idx) {
    Personagens[idx].Posicao.x = clampf(Personagens[idx].Posicao.x, Min.x + 0.5f, Max.x - 0.5f);
    Personagens[idx].Posicao.y = clampf(Personagens[idx].Posicao.y, Min.y + 0.5f, Max.y - 0.5f);
}

// ---------------------------------------------------------------------
// Transformações auxiliares (como no código base do prof.)
// ---------------------------------------------------------------------
void RotacionaAoRedorDeUmPonto(float alfa, Ponto P) {
    glTranslatef(P.x, P.y, P.z);
    glRotatef(alfa, 0,0,1);
    glTranslatef(-P.x, -P.y, -P.z);
}

// ---------------------------------------------------------------------
// Desenho matricial do “modelo” (como no base)
// ---------------------------------------------------------------------
void SetaCor(int cor) { defineCor(cor); }

void DesenhaCelula() {
    glBegin(GL_QUADS);
      glVertex2f(0,0);
      glVertex2f(0,1);
      glVertex2f(1,1);
      glVertex2f(1,0);
    glEnd();
}
void DesenhaBorda() {
    glBegin(GL_LINE_LOOP);
      glVertex2f(0,0);
      glVertex2f(0,1);
      glVertex2f(1,1);
      glVertex2f(1,0);
    glEnd();
}

void DesenhaPersonagemMatricial() {
    ModeloMatricial MM;
    int ModeloDoPersonagem = Personagens[PersonagemAtual].IdDoModelo;
    MM = Modelos[ModeloDoPersonagem];

    glPushMatrix();
    int larg = MM.nColunas;
    int alt  = MM.nLinhas;
    for (int i=0;i<alt;i++) {
        glPushMatrix();
        for (int j=0;j<larg;j++) {
            int cor = MM.getColor(alt-1-i, j);
            if (cor != -1) {
                SetaCor(cor);
                DesenhaCelula();
                defineCor(Wheat);
                DesenhaBorda();
            }
            glTranslatef(1,0,0);
        }
        glPopMatrix();
        glTranslatef(0,1,0);
    }
    glPopMatrix();
}

// ---------------------------------------------------------------------
// Eixos (debug)
// ---------------------------------------------------------------------
void DesenhaEixos() {
    Ponto Meio;
    Meio.x = (Max.x+Min.x)/2;
    Meio.y = (Max.y+Min.y)/2;
    Meio.z = (Max.z+Min.z)/2;

    glBegin(GL_LINES);
      glVertex2f(Min.x,Meio.y); glVertex2f(Max.x,Meio.y);
      glVertex2f(Meio.x,Min.y); glVertex2f(Meio.x,Max.y);
    glEnd();
}

// ---------------------------------------------------------------------
// Modelos (lidos de arquivo)
// Ajuste os nomes conforme seus arquivos existentes no projeto.
// Se algum não existir, fazemos fallback no modelo 0.
// ---------------------------------------------------------------------
bool TentaLerModelo(int idDst, const char* filename) {
    // Retorna true se conseguiu ler; false se falhou
    ifstream f(filename);
    if (!f.good()) {
        cerr << "[AVISO] Nao encontrei '" << filename
             << "'. Vou usar o modelo 0 como fallback.\n";
        Modelos[idDst] = Modelos[ID_MODELO_JOGADOR]; // fallback
        return false;
    }
    f.close();
    Modelos[idDst].leModelo(filename);
    return true;
}

void CarregaModelos() {
    // 0: Jogador; 1: Projetil; 2..: Naves
    Modelos[ID_MODELO_JOGADOR].leModelo("MatrizExemplo0.txt"); // jogador
    Modelos[ID_MODELO_PROJETIL].leModelo("MatrizProjetil.txt"); // projétil

    // Tente carregar pelo menos 4 modelos de naves:
    // Ajuste estes nomes para os que você realmente tem no seu repositório.
    // Exemplos comuns no material do prof:
    // "NaveCaca.txt", "NavePassageiros.txt", "NaveInimiga1.txt", "NaveInimiga2.txt"
    const char* candidatos[] = {
        "NaveCaca.txt",
        "NavePassageiros.txt",
        "NaveInimiga1.txt",
        "NaveInimiga2.txt"
    };

    int id = ID_MODELO_INICIO_NAVES;
    for (const char* nome : candidatos) {
        TentaLerModelo(id, nome);
        id++;
    }
    nModelos = max(nModelos, id); // atualiza nModelos
}

// ---------------------------------------------------------------------
// Envelope OOBB (como no base) e teste de colisão
// ---------------------------------------------------------------------
bool HaInterseccao(Ponto A, Ponto B, Ponto C, Ponto D); // deve existir em seu projeto base

void AtualizaEnvelope(int personagem) {
    Instancia I = Personagens[personagem];
    ModeloMatricial MM = Modelos[I.IdDoModelo];

    Ponto V = I.Direcao * (MM.nColunas/2.0);
    V.rotacionaZ(90);
    Ponto A = I.PosicaoDoPersonagem + V;

    Ponto B = A + I.Direcao*(MM.nLinhas);

    V = I.Direcao * (MM.nColunas);
    V.rotacionaZ(-90);
    Ponto C = B + V;

    V = -I.Direcao * (MM.nLinhas);
    Ponto D = C + V;

    if (desenhaEnvelope) {
        defineCor(Red);
        glBegin(GL_LINE_LOOP);
          glVertex2f(A.x,A.y); glVertex2f(B.x,B.y);
          glVertex2f(C.x,C.y); glVertex2f(D.x,D.y);
        glEnd();
    }

    Personagens[personagem].Envelope[0] = A;
    Personagens[personagem].Envelope[1] = B;
    Personagens[personagem].Envelope[2] = C;
    Personagens[personagem].Envelope[3] = D;
}

bool TestaColisao(int Objeto1, int Objeto2) {
    for(int i=0;i<4;i++) {
        Ponto A = Personagens[Objeto1].Envelope[i];
        Ponto B = Personagens[Objeto1].Envelope[(i+1)%4];
        for(int j=0;j<4;j++) {
            Ponto C = Personagens[Objeto2].Envelope[j];
            Ponto D = Personagens[Objeto2].Envelope[(j+1)%4];
            if (HaInterseccao(A,B,C,D)) return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------
// Criação das instâncias do jogo
//   0 = jogador; 1..K = inimigos; demais = tiros
// ---------------------------------------------------------------------
int PrimeiroInimigo = 1;
int QtdInimigos     = 12;   // você pode ajustar aqui
float VelMinInimigo = 0.5f;
float VelMaxInimigo = 2.0f;

// probabilidade de cada inimigo atirar por segundo (~lambda/seg)
float TaxaTiroInimigo = 0.7f;

void CriaJogador() {
    int i = 0;
    float ang = -90; // apontando “para cima” na lógica do modelo (ajuste se desejar)

    Personagens[i].Posicao = Ponto(-10, 0);
    Personagens[i].Escala  = Ponto(1,1);
    Personagens[i].Rotacao = ang;
    Personagens[i].IdDoModelo = ID_MODELO_JOGADOR;
    Personagens[i].modelo  = DesenhaPersonagemMatricial;
    Personagens[i].Pivot   = Ponto(2.5, 0);
    Personagens[i].Direcao = Ponto(0,1);
    Personagens[i].Direcao.rotacionaZ(ang);
    Personagens[i].Velocidade = 0;
    Personagens[i + AREA_DE_BACKUP] = Personagens[i]; // backup para respawn
}

bool ColideComAlgum(int idx) {
    for (int j=0; j<idx; ++j) {
        if (j == 0) continue; // não colidir com o jogador no spawn
        if (TestaColisao(j, idx)) return true;
    }
    return false;
}

void CriaInimigos() {
    // Posiciona aleatoriamente, sem colisão inicial e dentro dos limites
    int i = PrimeiroInimigo;
    int ultimo = PrimeiroInimigo + QtdInimigos;

    std::uniform_real_distribution<float> rx(Min.x+2.0f, Max.x-2.0f);
    std::uniform_real_distribution<float> ry(Min.y+2.0f, Max.y-2.0f);
    std::uniform_real_distribution<float> rang(0.0f, 360.0f);
    std::uniform_real_distribution<float> rvel(VelMinInimigo, VelMaxInimigo);

    for (; i<ultimo; ++i) {
        bool ok = false;
        int tentativas = 0;
        do {
            tentativas++;
            float ang = rang(rng);

            Personagens[i].Posicao = Ponto(rx(rng), ry(rng));
            Personagens[i].Escala  = Ponto(1,1);
            Personagens[i].Rotacao = ang;
            // Escolhe um modelo de nave (2..5) ciclando
            int idNav = ID_MODELO_INICIO_NAVES + ((i-PrimeiroInimigo) % 4);
            Personagens[i].IdDoModelo = idNav;
            Personagens[i].modelo     = DesenhaPersonagemMatricial;
            Personagens[i].Pivot      = Ponto(0.5,0);
            Personagens[i].Direcao    = Ponto(0,1);
            Personagens[i].Direcao.rotacionaZ(ang);
            Personagens[i].Velocidade = rvel(rng);

            // precisa do envelope para testar colisão
            AtualizaEnvelope(i);
            ok = !ColideComAlgum(i);
        } while(!ok && tentativas < 50);

        Personagens[i + AREA_DE_BACKUP] = Personagens[i]; // backup para respawn (se quiser)
    }

    nInstancias = ultimo; // até aqui são jogador + inimigos
}

// ---------------------------------------------------------------------
// Tiros
//   Dono do tiro é guardado em Personagens[i].Pivot.z (0/1/2)
//   Limitamos 20 tiros do jogador simultâneos
// ---------------------------------------------------------------------
int ContaTirosDoDono(int dono) {
    int c=0;
    for (int i=PrimeiroInimigo+QtdInimigos; i<nInstancias; ++i) {
        if (Personagens[i].IdDoModelo == ID_MODELO_PROJETIL &&
            (int)std::round(Personagens[i].Pivot.z) == dono) c++;
    }
    return c;
}

void RemoveInstancia(int idx) {
    if (idx < 0 || idx >= nInstancias) return;
    // “remove” colocando a última instância ativa no lugar
    if (idx != nInstancias-1) {
        Personagens[idx] = Personagens[nInstancias-1];
    }
    nInstancias--;
}

void CriaTiro2(int nAtirador) { // conforme orientação do Moodle
    if (nInstancias >= MAX_INSTANCIAS-1) return;

    int i = nInstancias;
    Instancia Atirador = Personagens[nAtirador];

    float ang = Atirador.Rotacao;

    Personagens[i].Escala = Ponto(1,1);
    Personagens[i].Rotacao = ang;
    Personagens[i].IdDoModelo = ID_MODELO_PROJETIL;
    Personagens[i].modelo = DesenhaPersonagemMatricial;
    Personagens[i].Pivot = Ponto(0.5,0);

    Personagens[i].Direcao = Ponto(0,1);
    Personagens[i].Direcao.rotacionaZ(ang);

    int Altura = Modelos[Atirador.IdDoModelo].nLinhas;
    // sai um pouco à frente do atirador
    Ponto P = Atirador.PosicaoDoPersonagem + Atirador.Direcao*Altura * 1.05f - Personagens[i].Pivot;

    Personagens[i].Posicao = P;

    // tag de dono do tiro (1 = jogador, 2 = inimigo)
    int dono = (nAtirador==0) ? OWNER_JOGADOR : OWNER_INIMIGO;
    Personagens[i].Pivot.z = (float)dono;

    // velocidade maior que a do atirador
    Personagens[i].Velocidade = (nAtirador==0) ? 3.0f : 2.5f;

    Personagens[i + AREA_DE_BACKUP] = Personagens[i];
    nInstancias++;
}

void CriaTiro() { // versão sem parâmetro (atira o jogador)
    // limita 20 tiros simultâneos do jogador
    if (ContaTirosDoDono(OWNER_JOGADOR) >= MAX_TIROS_JOGADOR) return;
    CriaTiro2(0);
}

// ---------------------------------------------------------------------
// Atualização de envelopes, colisões e lógica do jogo
// ---------------------------------------------------------------------
void AtualizaTodosEnvelopes() {
    for (int i=0; i<nInstancias; ++i) AtualizaEnvelope(i);
}

bool EhTiroDoJogador(int i) {
    return Personagens[i].IdDoModelo == ID_MODELO_PROJETIL &&
           (int)std::round(Personagens[i].Pivot.z) == OWNER_JOGADOR;
}
bool EhTiroDoInimigo(int i) {
    return Personagens[i].IdDoModelo == ID_MODELO_PROJETIL &&
           (int)std::round(Personagens[i].Pivot.z) == OWNER_INIMIGO;
}
bool EhInimigo(int i) {
    return Personagens[i].IdDoModelo >= ID_MODELO_INICIO_NAVES;
}

void AtualizaJogo() {
    // 1) Atualiza envelopes
    AtualizaTodosEnvelopes();

    // 2) Colisões tiro do jogador x inimigos
    for (int i=PrimeiroInimigo+QtdInimigos; i<nInstancias; ++i) {
        if (!EhTiroDoJogador(i)) continue;
        for (int j=PrimeiroInimigo; j<PrimeiroInimigo+QtdInimigos; ++j) {
            if (j >= nInstancias) break;           // se já removemos todos
            if (!EhInimigo(j)) continue;
            if (TestaColisao(i, j)) {
                // remove inimigo e tiro
                RemoveInstancia(j);
                RemoveInstancia(i);
                // como removemos j (colocando a última no lugar),
                // precisamos reiniciar loop externo de inimigos
                // e também garantir que i ainda é válido
                AtualizaTodosEnvelopes();
                break;
            }
        }
    }

    // 3) Colisões tiro de inimigo x jogador
    for (int i=PrimeiroInimigo+QtdInimigos; i<nInstancias; ++i) {
        if (!EhTiroDoInimigo(i)) continue;
        if (TestaColisao(0, i)) {
            // jogador perde vida, respawna
            Vidas--;
            cout << "Levou um tiro! Vidas = " << Vidas << endl;
            // restaura do backup
            Personagens[0] = Personagens[0 + AREA_DE_BACKUP];
            RemoveInstancia(i);
            AtualizaTodosEnvelopes();
            if (Vidas <= 0) {
                cout << "DERROTA. Vidas acabaram." << endl;
                exit(0);
            }
        }
    }

    // 4) Remove tiros que saíram da tela
    for (int i=PrimeiroInimigo+QtdInimigos; i<nInstancias; /*nada*/) {
        bool tiro = Personagens[i].IdDoModelo == ID_MODELO_PROJETIL;
        if (tiro) {
            Ponto p = Personagens[i].Posicao;
            if (p.x < Min.x-2 || p.x > Max.x+2 || p.y < Min.y-2 || p.y > Max.y+2) {
                RemoveInstancia(i);
                continue; // não incrementa i, pois a última veio para i
            }
        }
        ++i;
    }

    // 5) Vitória: se não há mais inimigos
    int vivos = 0;
    for (int i=PrimeiroInimigo; i<nInstancias; ++i) if (EhInimigo(i)) vivos++;
    if (vivos == 0) {
        cout << "VITORIA! Todas as naves inimigas foram destruidas." << endl;
        exit(0);
    }
}

// ---------------------------------------------------------------------
// Atualização do movimento (posições) — física simples
// ---------------------------------------------------------------------
void AtualizaPersonagens(float dt) {
    // Jogador dentro da tela + move
    Personagens[0].AtualizaPosicao(dt);
    MantemDentroDosLimites(0);

    // Inimigos e tiros
    for (int i=PrimeiroInimigo; i<nInstancias; ++i) {
        Personagens[i].AtualizaPosicao(dt);
        if (EhInimigo(i)) {
            // Evita sair da tela: se bater, vira a direção
            Ponto p = Personagens[i].Posicao;
            if (p.x <= Min.x+1 || p.x >= Max.x-1) {
                Personagens[i].Direcao.rotacionaZ(180);
                Personagens[i].Rotacao += 180;
            }
            if (p.y <= Min.y+1 || p.y >= Max.y-1) {
                Personagens[i].Direcao.rotacionaZ(180);
                Personagens[i].Rotacao += 180;
            }
            MantemDentroDosLimites(i);
        }
    }

    AtualizaJogo();
}

// ---------------------------------------------------------------------
// IA simples dos inimigos (trocas aleatórias de direção + tiros aleatórios)
// Chamado a ~10 Hz (100 ms) para não oscilar demais
// ---------------------------------------------------------------------
void AtualizaInimigosIA(float dt) {
    // probabilidade de trocar direção
    float pTroca = 0.5f * dt; // ~0.5 trocas/seg

    for (int i=PrimeiroInimigo; i<PrimeiroInimigo+QtdInimigos && i<nInstancias; ++i) {
        if (!EhInimigo(i)) continue;

        // troca aleatória de direção
        if (u01(rng) < pTroca) {
            float delta = (u01(rng) * 60.0f) - 30.0f; // [-30..+30] graus
            Personagens[i].Rotacao += delta;
            Personagens[i].Direcao.rotacionaZ(delta);
        }

        // tiro aleatório independente
        // taxa por segundo = TaxaTiroInimigo
        float pTiro = TaxaTiroInimigo * dt;
        if (u01(rng) < pTiro && ContaTirosDoDono(OWNER_INIMIGO) < MAX_TIROS_INIMIGOS) {
            CriaTiro2(i);
        }
    }
}

// ---------------------------------------------------------------------
// Renderização
// ---------------------------------------------------------------------
void DesenhaPersonagens() {
    for (int i=0; i<nInstancias; ++i) {
        PersonagemAtual = i;
        Personagens[i].desenha();
    }
}

// ---------------------------------------------------------------------
// GLUT: display / reshape / animate
// ---------------------------------------------------------------------
void display() {
    glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    DesenhaEixos();
    DesenhaPersonagens();

    glutSwapBuffers();
}

void reshape(int w, int h) {
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glViewport(0, 0, w, h);
    glOrtho(Min.x, Max.x, Min.y, Max.y, -10, +10);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

void animate() {
    double dt = T.getDeltaT();
    AccumDeltaT += dt;
    AccumLogic  += dt;

    // taxa de atualização da tela: 30 FPS
    if (AccumDeltaT >= 1.0/30.0) {
        AccumDeltaT = 0.0;
        glutPostRedisplay();
    }
    // lógica: ~100 ms (10 Hz) para a IA; e movimento em tempo contínuo
    if (AccumLogic >= 0.1) {
        float step = (float)AccumLogic;
        AccumLogic = 0.0f;
        AtualizaInimigosIA(step);
    }
    // movimento contínuo
    AtualizaPersonagens((float)dt);
}

// ---------------------------------------------------------------------
// Teclado
// ---------------------------------------------------------------------
void ContaTempo(double tempo) { // teste opcional
    Temporizador Tloc;
    unsigned long cont = 0;
    cout << "Inicio contagem de " << tempo << "s ..." << flush;
    while(true) {
        tempo -= Tloc.getDeltaT();
        cont++;
        if (tempo <= 0.0) {
            cout << "fim! Frames: " << cont << endl;
            break;
        }
    }
}

void keyboard(unsigned char key, int, int) {
    switch(key) {
        case 27: exit(0); break;                 // ESC sai do jogo
        case 't': ContaTempo(3); break;          // teste
        case ' ':
            CriaTiro();                           // tiro do jogador
            break;
        case 'e':
            desenhaEnvelope = !desenhaEnvelope;  // alterna envelopes
            break;
        default: break;
    }
}

void arrow_keys(int a_keys, int, int) {
    switch(a_keys) {
        case GLUT_KEY_LEFT:
            Personagens[0].Rotacao += 5;
            Personagens[0].Direcao.rotacionaZ(5);
            break;
        case GLUT_KEY_RIGHT:
            Personagens[0].Rotacao -= 5;
            Personagens[0].Direcao.rotacionaZ(-5);
            break;
        case GLUT_KEY_UP:
            Personagens[0].Velocidade += 0.5f;
            break;
        case GLUT_KEY_DOWN:
            Personagens[0].Velocidade -= 0.5f;
            break;
        default: break;
    }
}

// ---------------------------------------------------------------------
// Init e Main
// ---------------------------------------------------------------------
void init() {
    glClearColor(0.05f, 0.05f, 0.08f, 1.0f);

    CarregaModelos();

    float d = 20.0f;
    Min = Ponto(-d, -d);
    Max = Ponto( d,  d);

    // cria jogador e inimigos
    CriaJogador();
    CriaInimigos();
}

int main(int argc, char** argv) {
    cout << "Programa OpenGL - T1 CG" << endl;

    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_DEPTH | GLUT_RGB);
    glutInitWindowPosition(50, 50);
    glutInitWindowSize(800, 800);
    glutCreateWindow("T1 CG - Transformacoes Geometricas");

    init();

    glutDisplayFunc(display);
    glutIdleFunc(animate);
    glutReshapeFunc(reshape);
    glutKeyboardFunc(keyboard);
    glutSpecialFunc(arrow_keys);

    glutMainLoop();
    return 0;
}
