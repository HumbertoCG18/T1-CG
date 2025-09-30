// **********************************************************************
// PUCRS/Escola Politecnica
// COMPUTACAO GRAFICA - Trabalho 1 (2025/2)
// Autor: Humberto (adaptação/integração orientada)
// **********************************************************************
// Nesta versão:
// - MENU (Iniciar, Ajuda, Sair)
// - Countdown 3s (sem spawn antes do 0)
// - HUD: tempo, inimigos, vidas (top-left) e score (top-right)
// - Movimentação suave (WASD / setas): aceleração, freio, atrito
// - Rotação do jogador suavizada (vel. angular, aceleração e damping)
// - Tiros coloridos por dono; projétil do jogador mais rápido
// - Projéteis como bolinhas finas (escala configurável)
// - Delay de tiro para o jogador (cooldown)
// - Respawn dinâmico (não nasce em cima do player) e gradual,
//   com cooldown que diminui conforme o Score aumenta
// - Limite de inimigos simultâneos em tela
// - Aspect ratio preservado; player preso ao viewport
// - Envelope correto (usa Posicao + Pivot)
// - Overlay do player: nariz (frente) e chama (trás) colados e centralizados
// **********************************************************************

#include <iostream>
#include <cmath>
#include <ctime>
#include <fstream>
#include <vector>
#include <random>
#include <algorithm>
#include <sstream>
#include <iomanip>

using namespace std;

#include <GL/glut.h>

#include "Ponto.h"
#include "Instancia.h"
#include "ModeloMatricial.h"
#include "Temporizador.h"
#include "ListaDeCoresRGB.h"
#include "Linha.h" // HaInterseccao(...)

// ---------------------------------------------------------------------
// Estados do jogo
// ---------------------------------------------------------------------
enum class GameState { MENU, HELP, COUNTDOWN, PLAYING, PAUSED, GAMEOVER };
GameState gState = GameState::MENU;

// ---------------------------------------------------------------------
// Temporizadores e estado global
// ---------------------------------------------------------------------
Temporizador T;              // animação
double AccumDeltaT = 0.0;
double AccumLogic  = 0.0;    // IA/spawn
double gameTime    = 0.0;

bool   gameActive = false;   // só true no PLAYING
double countdownRemaining = 3.0;
double goTimer = 0.0;

// Instâncias
constexpr int MAX_INSTANCIAS = 500;
constexpr int AREA_DE_BACKUP = 250;
Instancia Personagens[MAX_INSTANCIAS + AREA_DE_BACKUP];

ModeloMatricial Modelos[32];
int nInstancias   = 0;
int nModelos      = 0;

// Índices de modelos
int ID_MODELO_JOGADOR      = 0;
int ID_MODELO_PROJETIL     = 1;
int ID_MODELO_INICIO_NAVES = 2;

// Jogo
constexpr int   MAX_TIROS_JOGADOR  = 20;
constexpr int   MAX_TIROS_INIMIGOS = 100;
int   Vidas = 3;
int   Score = 0;

// Dono do tiro (guardar em Pivot.z)
enum DonoDoTiro { OWNER_NINGUEM = 0, OWNER_JOGADOR = 1, OWNER_INIMIGO = 2 };

// Mundo base (quadrado) e viewport dinâmico
Ponto Min, Max;          // base: -d..+d
Ponto ViewMin, ViewMax;  // retângulo ajustado ao aspecto da janela

// Para HUD: conversão px -> unidades de mundo
int   gWinW = 800, gWinH = 800;
float gWorldPerPixelX = 0.05f;
float gWorldPerPixelY = 0.05f;

// Debug
bool desenhaEnvelope = false;

// RNG
std::mt19937_64 rng(123456);
std::uniform_real_distribution<float> u01(0.0f, 1.0f);

// Aux
int PersonagemAtual = 0;

// Respawn (agendamento gradual)
int    pendingSpawns = 0;    // quantos ainda faltam entrar gradualmente
double spawnAccum    = 0.0;  // acumulador de tempo para spawn

// Inimigos (primeira leva total — entram um a um)
int   PrimeiroInimigo = 1;
int   QtdInimigos     = 10;
float VelMinInimigo   = 0.7f;
float VelMaxInimigo   = 2.2f;

// ---------------------------------------------------------------------
// Controles SUAVES (key states) + rotação fluida
// ---------------------------------------------------------------------
bool keyDown[256] = {false};
bool spDown[512]  = {false};  // teclas especiais
bool gThrusting   = false;    // está acelerando (para desenhar chama)

const float ROT_SPEED_DEG = 180.0f;   // velocidade angular alvo (deg/s)
const float THRUST_ACC    = 7.0f;     // aceleração linear
const float BRAKE_ACC     = 12.0f;    // freio linear
const float DAMPING       = 1.5f;     // atrito linear (por segundo)
const float VEL_MAX       = 6.0f;     // teto de velocidade linear

// Rotação suave:
float gPlayerAngVel = 0.0f;           // velocidade angular atual (deg/s)
const float ANG_ACCEL_DEG = 720.0f;   // aceleração angular (deg/s^2)
const float ANG_DAMP       = 8.0f;    // amortecimento angular (1/s)

// Projéteis: velocidade e escala visual (bolinha)
const float PLAYER_BULLET_SPEED = 16.0f; // rápido (estilo Space Invaders “turbinado”)
const float ENEMY_BULLET_SPEED  = 4.5f;
const float BULLET_SCALE        = 0.38f; // bolinha pequena/fina

// Cooldown de tiro do player
const float  PLAYER_FIRE_COOLDOWN = 0.18f; // seg entre tiros
double gFireCooldownLeft = 0.0;

// Limite de inimigos simultâneos
const int   MAX_ENEMIES_ONSCREEN = 8;

// Constante PI local (evita depender de M_PI)
const float PI_F = 3.14159265358979323846f;

// ---------------------------------------------------------------------
// Utilidades
// ---------------------------------------------------------------------
static inline float clampf(float v, float a, float b) {
    return std::max(a, std::min(v, b));
}
static inline float dist2(const Ponto& a, const Ponto& b) {
    float dx = a.x - b.x, dy = a.y - b.y;
    return dx*dx + dy*dy;
}

void MantemDentroDosLimites(int idx) {
    // usa o viewport atual (sem “sair da visão”)
    Personagens[idx].Posicao.x = clampf(Personagens[idx].Posicao.x, ViewMin.x + 0.5f, ViewMax.x - 0.5f);
    Personagens[idx].Posicao.y = clampf(Personagens[idx].Posicao.y, ViewMin.y + 0.5f, ViewMax.y - 0.5f);
}

int ContaInimigosVivos() {
    int c = 0;
    for (int i=0; i<nInstancias; ++i)
        if (Personagens[i].IdDoModelo >= ID_MODELO_INICIO_NAVES) c++;
    return c;
}

// ---------------------------------------------------------------------
// Desenho matricial + cores dos projéteis + tinta do PLAYER
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

bool EhInimigo(int i);
bool EhTiroDoJogador(int i);
bool EhTiroDoInimigo(int i);

void DesenhaPersonagemMatricial() {
    ModeloMatricial MM = Modelos[ Personagens[PersonagemAtual].IdDoModelo ];
    bool isProjectile = (Personagens[PersonagemAtual].IdDoModelo == ID_MODELO_PROJETIL);
    bool isPlayer     = (PersonagemAtual == 0);
    int owner = (int)std::round(Personagens[PersonagemAtual].Pivot.z);

    glPushMatrix();

    // ----------------- PROJÉTIL COMO BOLINHA -----------------
    if (isProjectile) {
        // centro e raio em coordenadas do "modelo" (antes da escala da Instancia)
        float cx = MM.nColunas * 0.5f;
        float cy = MM.nLinhas  * 0.5f;
        float r  = 0.48f * std::min(MM.nColunas, MM.nLinhas);

        // cor por dono
        if (owner == OWNER_JOGADOR) glColor3f(0.1f, 1.0f, 1.0f);     // ciano
        else                        glColor3f(1.0f, 0.35f, 0.10f);    // laranja

        // preenchimento
        glBegin(GL_TRIANGLE_FAN);
          glVertex2f(cx, cy);
          const int N = 20;
          for (int k=0; k<=N; ++k) {
              float a = (2.0f*PI_F * k) / N;
              glVertex2f(cx + r*cosf(a), cy + r*sinf(a));
          }
        glEnd();
        // borda branca
        glColor3f(1,1,1);
        glBegin(GL_LINE_LOOP);
          const int NB = 20;
          for (int k=0; k<NB; ++k) {
              float a = (2.0f*PI_F * k) / NB;
              glVertex2f(cx + r*cosf(a), cy + r*sinf(a));
          }
        glEnd();

        glPopMatrix();
        return; // importante: não desenhar “grid” do modelo para projétil
    }
    // ----------------- FIM PROJÉTIL -----------------

    int larg = MM.nColunas;
    int alt  = MM.nLinhas;

    for (int i=0;i<alt;i++) {
        glPushMatrix();
        for (int j=0;j<larg;j++) {
            int cor = MM.getColor(alt-1-i, j);
            if (cor != -1) {
                if (isPlayer) {
                    glColor3f(0.2f, 0.95f, 0.85f); // player: teal claro
                    DesenhaCelula();
                    glColor3f(0.06f, 0.3f, 0.28f); // borda escura
                    DesenhaBorda();
                } else {
                    SetaCor(cor);
                    DesenhaCelula();
                    defineCor(Wheat);
                    DesenhaBorda();
                }
            }
            glTranslatef(1,0,0);
        }
        glPopMatrix();
        glTranslatef(0,1,0);
    }
    glPopMatrix();
}

// ---------------------------------------------------------------------
// UI e HUD
// ---------------------------------------------------------------------
void DesenhaEixos() {
    // opcional
    Ponto Meio;
    Meio.x = (ViewMax.x+ViewMin.x)/2;
    Meio.y = (ViewMax.y+ViewMin.y)/2;
    glBegin(GL_LINES);
      glVertex2f(ViewMin.x,Meio.y); glVertex2f(ViewMax.x,Meio.y);
      glVertex2f(Meio.x,ViewMin.y); glVertex2f(Meio.x,ViewMax.y);
    glEnd();
}

float StrokeTextWidth(const std::string& s) {
    int w = 0; for (char c : s) w += glutStrokeWidth(GLUT_STROKE_ROMAN, c);
    return (float)w;
}
void DrawCenteredStrokeText(const std::string& s, float cx, float cy, float desiredHeight, float r, float g, float b) {
    float scale = desiredHeight / 100.0f;
    float w = StrokeTextWidth(s) * scale;
    glPushMatrix();
        glTranslatef(cx - w/2.0f, cy - desiredHeight/3.0f, 0);
        glScalef(scale, scale, 1.0f);
        glColor3f(r,g,b);
        for (char c : s) glutStrokeCharacter(GLUT_STROKE_ROMAN, c);
    glPopMatrix();
}

int BitmapTextWidth(void* font, const std::string& s) {
    int w = 0; for (unsigned char c : s) w += glutBitmapWidth(font, c);
    return w;
}
void DrawBitmapTextLeft(const std::string& s, float x, float y, void* font = GLUT_BITMAP_9_BY_15, float r=1, float g=1, float b=1) {
    glColor3f(r,g,b);
    glRasterPos2f(x, y);
    for (unsigned char c : s) glutBitmapCharacter(font, c);
}
void DrawBitmapTextRight(const std::string& s, float xRight, float y, void* font = GLUT_BITMAP_9_BY_15, float r=1, float g=1, float b=1) {
    int px = BitmapTextWidth(font, s);
    float worldW = px * gWorldPerPixelX;
    glColor3f(r,g,b);
    glRasterPos2f(xRight - worldW, y);
    for (unsigned char c : s) glutBitmapCharacter(font, c);
}

void DrawCountdownOverlay() {
    float cx = (ViewMin.x+ViewMax.x)/2.0f;
    float cy = (ViewMin.y+ViewMax.y)/2.0f;
    float H  = (ViewMax.y-ViewMin.y)*0.25f;

    if (gState == GameState::COUNTDOWN) {
        int n = (int)ceil(countdownRemaining);
        n = max(1, min(3, n));
        std::string s = std::to_string(n);
        DrawCenteredStrokeText(s, cx+0.3f, cy-0.3f, H,   0.0f,0.0f,0.0f);
        DrawCenteredStrokeText(s, cx,      cy,      H,   1.0f,1.0f,1.0f);
    } else if (goTimer > 0.0) {
        std::string s = "GO!";
        DrawCenteredStrokeText(s, cx+0.3f, cy-0.3f, H*0.7f, 0.0f,0.0f,0.0f);
        DrawCenteredStrokeText(s, cx,      cy,      H*0.7f, 0.8f,1.0f,0.4f);
    }
}

void DrawHUD() {
    if (gState != GameState::PLAYING) return;
    std::ostringstream oss;
    oss << fixed << setprecision(1);
    oss << "Tempo: " << gameTime << "s"
        << "  |  Inimigos: " << ContaInimigosVivos()
        << "  |  Vidas: " << Vidas;
    DrawBitmapTextLeft(oss.str(), ViewMin.x + 0.5f, ViewMax.y - 1.0f);
    std::string s = "Score: " + std::to_string(Score);
    DrawBitmapTextRight(s, ViewMax.x - 0.5f, ViewMax.y - 1.0f);
}

// MENU ----------------------------------------------------------------
int menuIndex = 0;
const char* menuItems[] = {"Iniciar", "Ajuda", "Sair"};
const int MENU_COUNT = 3;

void DrawMenu() {
    float cx = (ViewMin.x+ViewMax.x)/2.0f;
    float cy = (ViewMin.y+ViewMax.y)/2.0f;
    float H  = (ViewMax.y-ViewMin.y)*0.14f;

    DrawCenteredStrokeText("T1 CG - Naves", cx, cy+H*2.0f, H*1.1f, 0.9f, 0.95f, 1.0f);

    for (int i=0;i<MENU_COUNT;i++) {
        float y = cy + (MENU_COUNT/2.0f - i) * (H*0.9f);
        float r = (i==menuIndex)? 0.95f : 0.7f;
        float g = (i==menuIndex)? 0.95f : 0.7f;
        float b = (i==menuIndex)? 0.4f  : 0.7f;
        DrawCenteredStrokeText(menuItems[i], cx, y, H*0.7f, r,g,b);
    }

    DrawBitmapTextLeft("Use ↑/↓ ou W/S para navegar. ENTER para selecionar.", ViewMin.x+0.8f, ViewMin.y+1.2f);
}

void DrawHelp() {
    float x = ViewMin.x+1.0f;
    float y = ViewMax.y-2.0f;

    DrawBitmapTextLeft("AJUDA:", x, y);
    y -= 1.0f;
    DrawBitmapTextLeft("- Setas ou WASD: girar (A/D ou ←/→), acelerar (W/↑), frear (S/↓)", x, y); y -= 0.8f;
    DrawBitmapTextLeft("- ESPACO: atirar (apos o jogo comecar)", x, y); y -= 0.8f;
    DrawBitmapTextLeft("- E: mostrar/ocultar envelopes", x, y); y -= 0.8f;
    DrawBitmapTextLeft("- ESC: voltar ao menu", x, y);
}

// ---------------------------------------------------------------------
// Modelos
// ---------------------------------------------------------------------
#define MODELS_DIR "models/"

bool TentaLerModelo(int idDst, const char* filename) {
    ifstream f(filename);
    if (!f.good()) {
        cerr << "[AVISO] Nao encontrei '" << filename
             << "'. Vou usar o modelo 0 como fallback.\n";
        Modelos[idDst] = Modelos[ID_MODELO_JOGADOR];
        return false;
    }
    f.close();
    Modelos[idDst].leModelo(filename);
    return true;
}

void CarregaModelos() {
    Modelos[ID_MODELO_JOGADOR].leModelo(MODELS_DIR "MatrizPlayer.txt");
    Modelos[ID_MODELO_PROJETIL].leModelo(MODELS_DIR "MatrizProjetil.txt");

    const char* candidatos[] = {
        MODELS_DIR "NaveCaca.txt",
        MODELS_DIR "NavePassageiros.txt",
        MODELS_DIR "NaveInimiga1.txt",
        MODELS_DIR "NaveInimiga2.txt"
    };
    int id = ID_MODELO_INICIO_NAVES;
    for (const char* nome : candidatos) {
        TentaLerModelo(id, nome);
        id++;
    }
    nModelos = max(nModelos, id);
}

// ---------------------------------------------------------------------
// Envelope (considera escala) + colisão
// ---------------------------------------------------------------------
bool HaInterseccao(Ponto A, Ponto B, Ponto C, Ponto D); // decl

void AtualizaEnvelope(int personagem) {
    Instancia I = Personagens[personagem];
    ModeloMatricial MM = Modelos[I.IdDoModelo];

    // Vetores base da orientação
    Ponto dir = I.Direcao;           // "cima" do modelo
    Ponto right = dir; right.rotacionaZ(90);

    // Dimensões em mundo
    float width  = MM.nColunas * I.Escala.x;
    float height = MM.nLinhas  * I.Escala.y;

    // Offset do pivot em mundo
    Ponto pivotWorld = right*(I.Pivot.x * I.Escala.x) + dir*(I.Pivot.y * I.Escala.y);

    // Origem do modelo (canto inferior esquerdo) em mundo
    Ponto origin = I.Posicao - pivotWorld;

    // Quatro cantos
    Ponto A = origin;                 // lower-left
    Ponto B = origin + dir*height;    // upper-left
    Ponto C = B + right*width;        // upper-right
    Ponto D = origin + right*width;   // lower-right

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

// Implementação: checa interseção entre os 4 lados de cada OOBB
bool TestaColisao(int Objeto1, int Objeto2) {
    for (int i = 0; i < 4; ++i) {
        Ponto A = Personagens[Objeto1].Envelope[i];
        Ponto B = Personagens[Objeto1].Envelope[(i + 1) % 4];
        for (int j = 0; j < 4; ++j) {
            Ponto C = Personagens[Objeto2].Envelope[j];
            Ponto D = Personagens[Objeto2].Envelope[(j + 1) % 4];
            if (HaInterseccao(A, B, C, D)) return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------
// Helpers: centros topo/base do player + overlay (nariz/chama)
// ---------------------------------------------------------------------
void GetTopBottomCentersPlayer(Ponto& topCenter, Ponto& bottomCenter) {
    // Usa a mesma transformação do envelope: origem = Posicao - pivotWorld
    Instancia& I = Personagens[0];
    ModeloMatricial& MM = Modelos[I.IdDoModelo];

    Ponto dir = I.Direcao;
    Ponto right = dir; right.rotacionaZ(90);

    float width  = MM.nColunas * I.Escala.x;
    float height = MM.nLinhas  * I.Escala.y;

    Ponto pivotWorld = right*(I.Pivot.x * I.Escala.x) + dir*(I.Pivot.y * I.Escala.y);
    Ponto origin = I.Posicao - pivotWorld;

    // Centros exatos das bordas: frente (topo) e trás (base)
    topCenter    = origin + dir*height + right*(width*0.5f);
    bottomCenter = origin + right*(width*0.5f);
}

void DrawPlayerOverlay() {
    // base exatamente na borda, centralizado
    Ponto topC, botC; GetTopBottomCentersPlayer(topC, botC);

    Instancia& I = Personagens[0];
    ModeloMatricial& MM = Modelos[I.IdDoModelo];

    Ponto dir = I.Direcao;         // frente
    Ponto right = dir; right.rotacionaZ(90);

    // Tamanho proporcional ao modelo (funciona com qualquer escala)
    float width  = MM.nColunas * I.Escala.x;
    float height = MM.nLinhas  * I.Escala.y;

    // “Nariz”: triângulo curto, colado na borda frontal
    float noseLen    = std::max(0.35f, height * 0.15f);   // comprimento pra fora
    float noseHalfW  = std::max(0.20f, width  * 0.12f);   // meia-base na própria borda

    Ponto noseTip = topC + dir * noseLen;
    Ponto noseL   = topC - right * noseHalfW;
    Ponto noseR   = topC + right * noseHalfW;

    glColor3f(0.98f, 1.0f, 1.0f);
    glBegin(GL_TRIANGLES);
      glVertex2f(noseTip.x, noseTip.y);
      glVertex2f(noseL.x,   noseL.y);
      glVertex2f(noseR.x,   noseR.y);
    glEnd();

    // “Chama”: triângulos colados na borda traseira (apenas quando acelerando)
    if (gThrusting) {
        float flameLen   = std::max(0.45f, height * 0.18f);
        float flameHalfW = std::max(0.22f, width  * 0.14f);

        // externa
        Ponto tip  = botC - dir * flameLen;
        Ponto flL  = botC - right * flameHalfW;
        Ponto flR  = botC + right * flameHalfW;

        glColor3f(1.0f, 0.45f, 0.10f);
        glBegin(GL_TRIANGLES);
          glVertex2f(tip.x, tip.y);
          glVertex2f(flL.x, flL.y);
          glVertex2f(flR.x, flR.y);
        glEnd();

        // interna (amarela), menor
        float inner = 0.55f;
        Ponto tip2 = botC - dir * (flameLen*0.65f);
        Ponto flL2 = botC - right * (flameHalfW*inner);
        Ponto flR2 = botC + right * (flameHalfW*inner);

        glColor3f(1.0f, 0.9f, 0.4f);
        glBegin(GL_TRIANGLES);
          glVertex2f(tip2.x, tip2.y);
          glVertex2f(flL2.x, flL2.y);
          glVertex2f(flR2.x, flR2.y);
        glEnd();
    }
}

// ---------------------------------------------------------------------
// Instâncias
// ---------------------------------------------------------------------
void CriaJogador() {
    int i = 0;
    float ang = -90;

    Personagens[i].Posicao = Ponto(0, 0);          // centro
    Personagens[i].Escala  = Ponto(0.7f, 0.7f);
    Personagens[i].Rotacao = ang;
    Personagens[i].IdDoModelo = ID_MODELO_JOGADOR;
    Personagens[i].modelo  = DesenhaPersonagemMatricial;
    Personagens[i].Pivot   = Ponto(2.5f, 0);
    Personagens[i].Direcao = Ponto(0,1);
    Personagens[i].Direcao.rotacionaZ(ang);
    Personagens[i].Velocidade = 0;
    Personagens[i + AREA_DE_BACKUP] = Personagens[i];

    nInstancias = 1; // conta o jogador
}

void RemoveInstancia(int idx) {
    if (idx < 0 || idx >= nInstancias) return;
    if (idx != nInstancias-1) {
        Personagens[idx] = Personagens[nInstancias-1];
    }
    nInstancias--;
}

bool SpawnOneEnemy() {
    if (nInstancias >= MAX_INSTANCIAS-1) return false;
    if (ContaInimigosVivos() >= MAX_ENEMIES_ONSCREEN) return false; // limite de simultâneos

    for (int j=0; j<nInstancias; ++j) AtualizaEnvelope(j);

    std::uniform_real_distribution<float> rx(ViewMin.x+2.0f, ViewMax.x-2.0f);
    std::uniform_real_distribution<float> ry(ViewMin.y+2.0f, ViewMax.y-2.0f);
    std::uniform_real_distribution<float> rang(0.0f, 360.0f);
    std::uniform_real_distribution<float> rvel(VelMinInimigo, VelMaxInimigo);

    const float safeR = 4.0f; // não nascer muito perto do player

    int idx = nInstancias;
    bool ok = false;
    for (int tent=0; tent<120 && !ok; ++tent) {
        float ang = rang(rng);

        Personagens[idx].Posicao = Ponto(rx(rng), ry(rng));
        Personagens[idx].Escala  = Ponto(0.6f, 0.6f);
        Personagens[idx].Rotacao = ang;

        int vivosNoMomento = std::max(0, idx - PrimeiroInimigo);
        int idNav = ID_MODELO_INICIO_NAVES + (vivosNoMomento % 4);

        Personagens[idx].IdDoModelo = idNav;
        Personagens[idx].modelo     = DesenhaPersonagemMatricial;
        Personagens[idx].Pivot      = Ponto(0.5,0);
        Personagens[idx].Direcao    = Ponto(0,1);
        Personagens[idx].Direcao.rotacionaZ(ang);
        Personagens[idx].Velocidade = rvel(rng);

        AtualizaEnvelope(idx);

        ok = true;
        if (dist2(Personagens[idx].Posicao, Personagens[0].Posicao) < safeR*safeR) ok = false;
        if (ok) {
            for (int j=0; j<nInstancias; ++j) {
                if (TestaColisao(j, idx)) { ok = false; break; }
            }
        }
    }
    if (!ok) return false;

    Personagens[idx + AREA_DE_BACKUP] = Personagens[idx];
    nInstancias++;
    return true;
}

// ---------------------------------------------------------------------
// Tiros
// ---------------------------------------------------------------------
int ContaTirosDoDono(int dono) {
    int c=0;
    for (int i=0; i<nInstancias; ++i) {
        if (Personagens[i].IdDoModelo == ID_MODELO_PROJETIL &&
            (int)std::round(Personagens[i].Pivot.z) == dono) c++;
    }
    return c;
}

void CriaTiro2(int nAtirador) {
    if (nInstancias >= MAX_INSTANCIAS-1) return;

    int i = nInstancias;
    Instancia Atirador = Personagens[nAtirador];
    float ang = Atirador.Rotacao;

    // Escala menor para a “bolinha”
    Personagens[i].Escala   = Ponto(BULLET_SCALE, BULLET_SCALE);
    Personagens[i].Rotacao  = ang;
    Personagens[i].IdDoModelo = ID_MODELO_PROJETIL;
    Personagens[i].modelo   = DesenhaPersonagemMatricial;
    Personagens[i].Pivot    = Ponto(0.5,0);

    Personagens[i].Direcao = Ponto(0,1);
    Personagens[i].Direcao.rotacionaZ(ang);

    int Altura = Modelos[Atirador.IdDoModelo].nLinhas;

    // sai mais à frente para não colidir com o atirador
    Ponto P = Atirador.Posicao + (Atirador.Direcao * (Altura * Atirador.Escala.y)) * 1.30f - (Personagens[i].Pivot);
    Personagens[i].Posicao = P;

    int dono = (nAtirador==0) ? OWNER_JOGADOR : OWNER_INIMIGO;
    Personagens[i].Pivot.z = (float)dono;

    // velocidade: jogador bem mais rápido
    float base = (nAtirador==0) ? PLAYER_BULLET_SPEED : ENEMY_BULLET_SPEED;
    Personagens[i].Velocidade = base + (nAtirador==0 ? std::max(0.0f, Atirador.Velocidade*0.3f) : 0.0f);

    Personagens[i + AREA_DE_BACKUP] = Personagens[i];
    nInstancias++;
}

void CriaTiro() {
    if (ContaTirosDoDono(OWNER_JOGADOR) >= MAX_TIROS_JOGADOR) return;
    CriaTiro2(0);
}

// ---------------------------------------------------------------------
// Lógica (envelopes/colisões/jogo)
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
    AtualizaTodosEnvelopes();

    // Tiro do jogador x inimigos
    for (int i=0; i<nInstancias; ) {
        if (!EhTiroDoJogador(i)) { ++i; continue; }
        bool hit = false;
        for (int j=0; j<nInstancias; ++j) {
            if (!EhInimigo(j)) continue;
            if (TestaColisao(i, j)) {
                Score += 100;
                int a = std::max(i, j);
                int b = std::min(i, j);
                RemoveInstancia(a);
                RemoveInstancia(b);
                pendingSpawns += (u01(rng) < 0.5f ? 1 : 2); // agenda novos gradualmente
                AtualizaTodosEnvelopes();
                hit = true;
                break;
            }
        }
        if (!hit) ++i;
    }

    // Tiros de inimigo x jogador
    for (int i=0; i<nInstancias; ) {
        if (!EhTiroDoInimigo(i)) { ++i; continue; }
        if (TestaColisao(0, i)) {
            Vidas--;
            Personagens[0] = Personagens[0 + AREA_DE_BACKUP];
            RemoveInstancia(i);
            AtualizaTodosEnvelopes();
            if (Vidas <= 0) {
                gState = GameState::GAMEOVER;
                gameActive = false;
                break;
            }
        } else {
            ++i;
        }
    }

    // Remove projéteis fora da tela
    for (int i=0; i<nInstancias; ) {
        bool tiro = (Personagens[i].IdDoModelo == ID_MODELO_PROJETIL);
        if (tiro) {
            Ponto p = Personagens[i].Posicao;
            if (p.x < ViewMin.x-2 || p.x > ViewMax.x+2 || p.y < ViewMin.y-2 || p.y > ViewMax.y+2) {
                RemoveInstancia(i);
                continue;
            }
        }
        ++i;
    }
}

// ---------------------------------------------------------------------
// Movimento, IA e respawn
// ---------------------------------------------------------------------
void HandlePlayerInput(float dt) {
    // ---- ROTAÇÃO SUAVE ----
    float rotDir = 0.0f;
    if (keyDown['a'] || spDown[GLUT_KEY_LEFT])  rotDir += 1.0f;
    if (keyDown['d'] || spDown[GLUT_KEY_RIGHT]) rotDir -= 1.0f;

    // velocidade angular alvo (deg/s)
    float targetAngVel = rotDir * ROT_SPEED_DEG;

    // aceleração até o alvo
    float diff = targetAngVel - gPlayerAngVel;
    float maxStep = ANG_ACCEL_DEG * dt;
    if (diff >  maxStep) diff =  maxStep;
    if (diff < -maxStep) diff = -maxStep;
    gPlayerAngVel += diff;

    // damping quando não há input
    if (rotDir == 0.0f) {
        gPlayerAngVel -= gPlayerAngVel * ANG_DAMP * dt;
    }

    float dAng = gPlayerAngVel * dt;
    if (fabs(dAng) > 0.0f) {
        Personagens[0].Rotacao += dAng;
        Personagens[0].Direcao.rotacionaZ(dAng);
    }

    // ---- ACELERAÇÃO / FREIO / ATRITO ----
    float thrust = 0.0f;
    if (keyDown['w'] || spDown[GLUT_KEY_UP])   thrust += THRUST_ACC;
    if (keyDown['s'] || spDown[GLUT_KEY_DOWN]) thrust -= BRAKE_ACC;

    gThrusting = (thrust > 0.0f);

    Personagens[0].Velocidade += thrust * dt;

    if (thrust <= 0.0f) {
        float v = Personagens[0].Velocidade;
        float dv = v * DAMPING * dt;
        Personagens[0].Velocidade = (v > dv) ? (v - dv) : 0.0f;
    }
    Personagens[0].Velocidade = clampf(Personagens[0].Velocidade, 0.0f, VEL_MAX);
}

void AtualizaPersonagens(float dt) {
    HandlePlayerInput(dt);

    // Jogador
    Personagens[0].AtualizaPosicao(dt);
    MantemDentroDosLimites(0);

    // Inimigos e tiros
    for (int i=1; i<nInstancias; ++i) {
        Personagens[i].AtualizaPosicao(dt);
        if (EhInimigo(i)) {
            Ponto p = Personagens[i].Posicao;
            if (p.x <= ViewMin.x+1 || p.x >= ViewMax.x-1) {
                Personagens[i].Direcao.rotacionaZ(180);
                Personagens[i].Rotacao += 180;
            }
            if (p.y <= ViewMin.y+1 || p.y >= ViewMax.y-1) {
                Personagens[i].Direcao.rotacionaZ(180);
                Personagens[i].Rotacao += 180;
            }
            MantemDentroDosLimites(i);
        }
    }

    AtualizaJogo();
}

void AtualizaInimigosIA(float dt) {
    float pTroca = 0.5f * dt;

    for (int i=1; i<nInstancias; ++i) {
        if (!EhInimigo(i)) continue;

        if (u01(rng) < pTroca) {
            float delta = (u01(rng) * 60.0f) - 30.0f;
            Personagens[i].Rotacao += delta;
            Personagens[i].Direcao.rotacionaZ(delta);
        }

        float pTiro = 0.7f * dt;
        if (u01(rng) < pTiro && ContaTirosDoDono(OWNER_INIMIGO) < MAX_TIROS_INIMIGOS) {
            CriaTiro2(i);
        }
    }
}

// cooldown dinâmico: acelera com o Score (até um mínimo)
float DynamicSpawnCooldown() {
    // base diminui conforme Score, mas com piso para não ficar absurdo
    // Fórmula suave: base / (1 + Score/K)
    const float base = 1.0f;      // s
    const float K    = 500.0f;    // a cada +500 pts dobra a taxa
    const float minc = 0.25f;     // piso
    float cd = base / (1.0f + (float)Score / K);
    return (cd < minc ? minc : cd);
}

void ProcessaRespawn(float dt) {
    if (pendingSpawns <= 0) return;

    spawnAccum += dt;
    float cd = DynamicSpawnCooldown();

    // Se já atingiu o limite, aguarda até matar alguém
    if (ContaInimigosVivos() >= MAX_ENEMIES_ONSCREEN) return;

    while (pendingSpawns > 0 && spawnAccum >= cd) {
        if (ContaInimigosVivos() >= MAX_ENEMIES_ONSCREEN) break;

        AtualizaTodosEnvelopes();
        if (SpawnOneEnemy()) pendingSpawns--;
        spawnAccum -= cd;
        cd = DynamicSpawnCooldown();
    }
}

// ---------------------------------------------------------------------
// Fluxos de jogo (Menu/Start/Reset)
// ---------------------------------------------------------------------
void ResetMatch() {
    nInstancias = 0;
    Vidas = 3;
    Score = 0;
    gameTime = 0.0;
    pendingSpawns = 0;
    spawnAccum = 0.0;
    gPlayerAngVel = 0.0f;
    gFireCooldownLeft = 0.0;
    CriaJogador();
    AtualizaTodosEnvelopes();
}

void StartCountdown() {
    gState = GameState::COUNTDOWN;
    gameActive = false;
    countdownRemaining = 3.0;
    goTimer = 0.0;
}

void BeginPlaying() {
    gState = GameState::PLAYING;
    gameActive = true;
    goTimer = 0.7;
    pendingSpawns = QtdInimigos; // serão criados gradualmente
    spawnAccum = 0.0;
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

    if (gState == GameState::MENU) {
        DrawMenu();
    } else if (gState == GameState::HELP) {
        DrawHelp();
    } else {
        DesenhaPersonagens();
        // overlay do player (clarifica frente/trás)
        if (nInstancias > 0) DrawPlayerOverlay();

        DrawHUD();
        DrawCountdownOverlay();
    }

    glutSwapBuffers();
}

void reshape(int w, int h) {
    gWinW = (w<=0?1:w);
    gWinH = (h<=0?1:h);

    float baseW = Max.x - Min.x;
    float baseH = Max.y - Min.y;
    float aspectWin = (float)gWinW / (float)gWinH;

    float viewH = baseH;
    float viewW = viewH * aspectWin;

    float cx = (Min.x + Max.x)*0.5f;
    float cy = (Min.y + Max.y)*0.5f;

    ViewMin.x = cx - viewW*0.5f;
    ViewMax.x = cx + viewW*0.5f;
    ViewMin.y = cy - viewH*0.5f;
    ViewMax.y = cy + viewH*0.5f;

    gWorldPerPixelX = (ViewMax.x - ViewMin.x) / (float)gWinW;
    gWorldPerPixelY = (ViewMax.y - ViewMin.y) / (float)gWinH;

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glViewport(0, 0, gWinW, gWinH);
    glOrtho(ViewMin.x, ViewMax.x, ViewMin.y, ViewMax.y, -10, +10);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

void animate() {
    double dt = T.getDeltaT();
    AccumDeltaT += dt;
    AccumLogic  += dt;

    // cooldown de tiro
    if (gFireCooldownLeft > 0.0) {
        gFireCooldownLeft -= dt;
        if (gFireCooldownLeft < 0.0) gFireCooldownLeft = 0.0;
    }

    if (gState == GameState::COUNTDOWN) {
        countdownRemaining -= dt;
        if (countdownRemaining <= 0.0) {
            countdownRemaining = 0.0;
            BeginPlaying(); // só aqui começa a entrar inimigo, gradualmente
        }
    } else if (gState == GameState::PLAYING) {
        gameTime += dt;
        if (goTimer > 0.0) goTimer -= dt;

        if (AccumLogic >= 0.1) {
            float step = (float)AccumLogic;
            AccumLogic = 0.0f;
            AtualizaInimigosIA(step);
            ProcessaRespawn(step);
        }
        AtualizaPersonagens((float)dt);
    }

    if (AccumDeltaT >= 1.0/30.0) {
        AccumDeltaT = 0.0;
        glutPostRedisplay();
    }
}

// ---------------------------------------------------------------------
// Teclado
// ---------------------------------------------------------------------
void ContaTempo(double tempo) {
    Temporizador Tloc;
    unsigned long cont = 0;
    cout << "Inicio contagem de " << tempo << "s ..." << flush;
    while(true) {
        tempo -= Tloc.getDeltaT();
        cont++;
        if (tempo <= 0.0) { cout << "fim! Frames: " << cont << endl; break; }
    }
}

void keyboardDown(unsigned char key, int, int) {
    keyDown[(unsigned char)key] = true;

    if (gState == GameState::MENU) {
        if (key == 13) { // ENTER
            if (menuIndex == 0) { // Iniciar
                ResetMatch();
                StartCountdown();
            } else if (menuIndex == 1) { // Ajuda
                gState = GameState::HELP;
            } else if (menuIndex == 2) { // Sair
                exit(0);
            }
        } else if (key == 'w' || key == 'W') {
            menuIndex = (menuIndex + MENU_COUNT - 1) % MENU_COUNT;
        } else if (key == 's' || key == 'S') {
            menuIndex = (menuIndex + 1) % MENU_COUNT;
        } else if (key == 27) { // ESC
            exit(0);
        }
        return;
    }

    if (gState == GameState::HELP) {
        if (key == 27) { gState = GameState::MENU; }
        return;
    }

    if (gState == GameState::PLAYING || gState == GameState::COUNTDOWN) {
        switch(key) {
            case 27: gState = GameState::MENU; break; // ESC volta ao menu
            case 't': ContaTempo(3); break;
            case ' ':
                if (gState == GameState::PLAYING && gFireCooldownLeft <= 0.0) {
                    CriaTiro();
                    gFireCooldownLeft = PLAYER_FIRE_COOLDOWN;
                }
                break;
            case 'e':
            case 'E': desenhaEnvelope = !desenhaEnvelope; break;
            default: break;
        }
    }
}
void keyboardUp(unsigned char key, int, int) {
    keyDown[(unsigned char)key] = false;
}

void specialDown(int k, int, int) {
    spDown[k] = true;
    if (gState == GameState::MENU) {
        if (k == GLUT_KEY_UP)   menuIndex = (menuIndex + MENU_COUNT - 1) % MENU_COUNT;
        if (k == GLUT_KEY_DOWN) menuIndex = (menuIndex + 1) % MENU_COUNT;
    }
}
void specialUp(int k, int, int) {
    spDown[k] = false;
}

// ---------------------------------------------------------------------
// Init / Main
// ---------------------------------------------------------------------
void init() {
    glClearColor(0.05f, 0.05f, 0.08f, 1.0f);

    CarregaModelos();

    float d = 20.0f;
    Min = Ponto(-d, -d);
    Max = Ponto( d,  d);

    reshape(gWinW, gWinH); // prepara viewport
}

int main(int argc, char** argv) {
    cout << "Programa OpenGL - T1 CG" << endl;

    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_DEPTH | GLUT_RGB);
    glutInitWindowPosition(50, 50);
    glutInitWindowSize(gWinW, gWinH);
    glutCreateWindow("T1 CG - Transformacoes Geometricas");

    glutIgnoreKeyRepeat(1);

    init();

    glutDisplayFunc(display);
    glutIdleFunc(animate);
    glutReshapeFunc(reshape);

    glutKeyboardFunc(keyboardDown);
    glutKeyboardUpFunc(keyboardUp);
    glutSpecialFunc(specialDown);
    glutSpecialUpFunc(specialUp);

    glutMainLoop();
    return 0;
}
