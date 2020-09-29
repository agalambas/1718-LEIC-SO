/*
// Projeto SO - exercicio 4
// Grupo 107
// 86372 Alexandre Galambas
// 86430 Guilherme Galambas
*/

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <math.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

#include "matrix2d.h"
#include "util.h"

/*--------------------------------------------------------------------
| Type: thread_info
| Description: Estrutura com Informacao para Trabalhadoras
---------------------------------------------------------------------*/

typedef struct {
  int    id;
  int    N;
  int    iter;
  int    tam_fatia;
  double maxD;
} thread_info;

/*--------------------------------------------------------------------
| Type: doubleBarrierWithMax
| Description: Barreira dupla com variavel de max-reduction
---------------------------------------------------------------------*/

typedef struct {
  int             total_nodes;
  int             pending[2];
  double          maxdelta[2];
  int             iteracoes_concluidas;
  pthread_mutex_t mutex;
  pthread_cond_t  wait[2];
} DualBarrierWithMax;

/*--------------------------------------------------------------------
| Global variables
---------------------------------------------------------------------*/

DoubleMatrix2D     *matrix_copies[2];
DualBarrierWithMax *dual_barrier;
double              maxD;
bool                fork_flag = false;
bool                finish = false;
int                 last;
char*               fichS;

/*--------------------------------------------------------------------
| Signals
---------------------------------------------------------------------*/

void alarmHandler(int k) {
  signal(SIGALRM, alarmHandler);
  fork_flag = true;
}

void exitHandler(int k) {
  signal(SIGINT, exitHandler);
  writeMatrix2dToFile(matrix_copies[last], fichS);
  exit(0);
}

/*--------------------------------------------------------------------
| Function: dualBarrierInit
| Description: Inicializa uma barreira dupla
---------------------------------------------------------------------*/

DualBarrierWithMax *dualBarrierInit(int ntasks, char *filename, int periodo) {
  DualBarrierWithMax *b;
  b = (DualBarrierWithMax*) malloc (sizeof(DualBarrierWithMax));
  if (b == NULL) return NULL;

  b->total_nodes = ntasks;
  b->pending[0]  = ntasks;
  b->pending[1]  = ntasks;
  b->maxdelta[0] = 0;
  b->maxdelta[1] = 0;
  b->iteracoes_concluidas = 0;

  if (pthread_mutex_init(&(b->mutex), NULL) != 0) {
    fprintf(stderr, "\nErro a inicializar mutex\n");
    exit(1);
  }
  if (pthread_cond_init(&(b->wait[0]), NULL) != 0) {
    fprintf(stderr, "\nErro a inicializar variável de condição\n");
    exit(1);
  }
  if (pthread_cond_init(&(b->wait[1]), NULL) != 0) {
    fprintf(stderr, "\nErro a inicializar variável de condição\n");
    exit(1);
  }
  return b;
}

/*--------------------------------------------------------------------
| Function: dualBarrierFree
| Description: Liberta os recursos de uma barreira dupla
---------------------------------------------------------------------*/

void dualBarrierFree(DualBarrierWithMax* b) {
  if (pthread_mutex_destroy(&(b->mutex)) != 0) {
    fprintf(stderr, "\nErro a destruir mutex\n");
    exit(1);
  }
  if (pthread_cond_destroy(&(b->wait[0])) != 0) {
    fprintf(stderr, "\nErro a destruir variável de condição\n");
    exit(1);
  }
  if (pthread_cond_destroy(&(b->wait[1])) != 0) {
    fprintf(stderr, "\nErro a destruir variável de condição\n");
    exit(1);
  }
  free(b);
}

/*--------------------------------------------------------------------
| Function: dualBarrierWait
| Description: Ao chamar esta funcao, a tarefa fica bloqueada ate que
|              o numero 'ntasks' de tarefas necessario tenham chamado
|              esta funcao, especificado ao ininializar a barreira em
|              dualBarrierInit(ntasks). Esta funcao tambem calcula o
|              delta maximo entre todas as threads e devolve o
|              resultado no valor de retorno
---------------------------------------------------------------------*/

double dualBarrierWait (DualBarrierWithMax* b, int current, double localmax) {
  int next = 1 - current;
  if (pthread_mutex_lock(&(b->mutex)) != 0) {
    fprintf(stderr, "\nErro a bloquear mutex\n");
    exit(1);
  }
  // decrementar contador de tarefas restantes
  b->pending[current]--;
  // actualizar valor maxDelta entre todas as threads
  if (b->maxdelta[current]<localmax)
    b->maxdelta[current]=localmax;
  // verificar se sou a ultima tarefa
  if (b->pending[current]==0) {
    // sim -- inicializar proxima barreira e libertar threads
    b->iteracoes_concluidas++;
    b->pending[next]  = b->total_nodes;
    b->maxdelta[next] = 0;
    last = current;
    if (pthread_cond_broadcast(&(b->wait[current])) != 0) {
      fprintf(stderr, "\nErro a assinalar todos em variável de condição\n");
      exit(1);
    }
  }
  else {
    // nao -- esperar pelas outras tarefas
    while (b->pending[current]>0) {
      if (pthread_cond_wait(&(b->wait[current]), &(b->mutex)) != 0) {
        fprintf(stderr, "\nErro a esperar em variável de condição\n");
        exit(1);
      }
    }
  }
  double maxdelta = b->maxdelta[current];
  if (pthread_mutex_unlock(&(b->mutex)) != 0) {
    fprintf(stderr, "\nErro a desbloquear mutex\n");
    exit(1);
  }
  return maxdelta;
}

/*--------------------------------------------------------------------
| Function: tarefa_trabalhadora
| Description: Funcao executada por cada tarefa trabalhadora.
|              Recebe como argumento uma estrutura do tipo thread_info
---------------------------------------------------------------------*/

void *tarefa_trabalhadora(void *args) {
  thread_info *tinfo = (thread_info *) args;
  int tam_fatia = tinfo->tam_fatia;
  int my_base = tinfo->id * tam_fatia;
  double global_delta = INFINITY;
  int iter = 0;

  do {
    int atual = iter % 2;
    int prox = 1 - iter % 2;
    double max_delta = 0;

    // Calcular Pontos Internos
    for (int i = my_base; i < my_base + tinfo->tam_fatia; i++) {
      for (int j = 0; j < tinfo->N; j++) {
        double val = (dm2dGetEntry(matrix_copies[atual], i,   j+1) +
                      dm2dGetEntry(matrix_copies[atual], i+2, j+1) +
                      dm2dGetEntry(matrix_copies[atual], i+1, j) +
                      dm2dGetEntry(matrix_copies[atual], i+1, j+2))/4;
        // calcular delta
        double delta = fabs(val - dm2dGetEntry(matrix_copies[atual], i+1, j+1));
        if (delta > max_delta) {
          max_delta = delta;
        }
        dm2dSetEntry(matrix_copies[prox], i+1, j+1, val);
      }
    }
    // barreira de sincronizacao; calcular delta global
    global_delta = dualBarrierWait(dual_barrier, atual, max_delta);
  } while (++iter < tinfo->iter && global_delta >= tinfo->maxD);

  if (tinfo->id==0) finish = true;
  return 0;
}

/*--------------------------------------------------------------------
| Function: main
| Description: Entrada do programa
---------------------------------------------------------------------*/

int main (int argc, char** argv) {
  int N;
  double tEsq, tSup, tDir, tInf;
  int iter, trab, periodoS;
  int tam_fatia;
  int res, pid;

  if (argc != 11) {
    fprintf(stderr, "Utilizacao: ./heatSim N tEsq tSup tDir tInf iter trab maxD fichS periodoS\n\n");
    die("Numero de argumentos invalido");
  }

  // Ler Input
  N        = parse_integer_or_exit(argv[1],  "N",       1);
  tEsq     = parse_double_or_exit (argv[2],  "tEsq",    0);
  tSup     = parse_double_or_exit (argv[3],  "tSup",    0);
  tDir     = parse_double_or_exit (argv[4],  "tDir",    0);
  tInf     = parse_double_or_exit (argv[5],  "tInf",    0);
  iter     = parse_integer_or_exit(argv[6],  "iter",    1);
  trab     = parse_integer_or_exit(argv[7],  "trab",    1);
  maxD     = parse_double_or_exit (argv[8],  "maxD",    0);
  periodoS = parse_integer_or_exit(argv[10], "peridoS", 0);
  fichS    = (char*) malloc(sizeof(char) * (strlen(argv[9])+1));
  fichS    = argv[9];

  if (N % trab != 0) {
    fprintf(stderr, "\nErro: Argumento %s e %s invalidos.\n"
                    "%s deve ser multiplo de %s.", "N", "trab", "N", "trab");
    return -1;
  }

  // Inicializar Barreira
  dual_barrier = dualBarrierInit(trab, fichS, periodoS);
  if (dual_barrier == NULL)
    die("Nao foi possivel inicializar barreira");

  // Inicializar sinais
  if(signal(SIGALRM, alarmHandler) == SIG_ERR || signal(SIGINT, exitHandler) == SIG_ERR)
    printf("Erro nos sinais");

  // Calcular tamanho de cada fatia
  tam_fatia = N / trab;

  // Criar e Inicializar Matrizes
  matrix_copies[0] = readMatrix2dFromFile(fopen(fichS, "r"), N+2, N+2);
  if (matrix_copies[0] == NULL) {

    matrix_copies[0] = dm2dNew(N+2,N+2);
    if (matrix_copies[0] == NULL)
      die("Erro ao criar matrizes");

    dm2dSetLineTo (matrix_copies[0], 0, tSup);
    dm2dSetLineTo (matrix_copies[0], N+1, tInf);
    dm2dSetColumnTo (matrix_copies[0], 0, tEsq);
    dm2dSetColumnTo (matrix_copies[0], N+1, tDir);
  }

  matrix_copies[1] = dm2dNew(N+2,N+2);
  if (matrix_copies[1] == NULL)
    die("Erro ao criar matrizes");

  dm2dCopy (matrix_copies[1],matrix_copies[0]);

  // Reservar memoria para trabalhadoras
  thread_info *tinfo = (thread_info*) malloc(trab * sizeof(thread_info));
  pthread_t *trabalhadoras = (pthread_t*) malloc(trab * sizeof(pthread_t));

  if (tinfo == NULL || trabalhadoras == NULL) {
    die("Erro ao alocar memoria para trabalhadoras");
  }

  // Criar trabalhadoras
  for (int i=0; i < trab; i++) {
    tinfo[i].id = i;
    tinfo[i].N = N;
    tinfo[i].iter = iter;
    tinfo[i].tam_fatia = tam_fatia;
    tinfo[i].maxD = maxD;
    res = pthread_create(&trabalhadoras[i], NULL, tarefa_trabalhadora, &tinfo[i]);
    if (res != 0) {
      die("Erro ao criar uma tarefa trabalhadora");
    }
  }

  // Espera pelo sinal para gravar
  alarm(periodoS);
  while(!finish && periodoS!=0) {
    if(fork_flag && waitpid(pid, NULL, WNOHANG)==0) {
      pid = fork();
      if (pid == 0) {
        writeMatrix2dToFile(matrix_copies[last], fichS);
        exit(0);
      }
      else if (pid < 0) die("Erro ao criar novo processo");
      fork_flag = false;
      alarm(periodoS);
    }
  }

  // Esperar que as trabalhadoras terminem
  for (int i=0; i<trab; i++) {
    res = pthread_join(trabalhadoras[i], NULL);
    if (res != 0)
      die("Erro ao esperar por uma tarefa trabalhadora");
  }

  dm2dPrint(matrix_copies[dual_barrier->iteracoes_concluidas%2]);

  // Libertar memoria
  unlink(fichS);
  dm2dFree(matrix_copies[0]);
  dm2dFree(matrix_copies[1]);
  free(tinfo);
  free(trabalhadoras);
  dualBarrierFree(dual_barrier);

  return 0;
}
