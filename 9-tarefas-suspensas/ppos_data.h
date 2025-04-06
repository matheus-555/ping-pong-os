// PingPongOS - PingPong Operating System
// Prof. Carlos A. Maziero, DINF UFPR
// Versão 1.5 -- Março de 2023

// Estruturas de dados internas do sistema operacional

#ifndef __PPOS_DATA__
#define __PPOS_DATA__

#include <ucontext.h>		// biblioteca POSIX de trocas de contexto
#include <stdbool.h>

typedef enum {
  TASK_STATE_RUNNING = 0,
  TASK_STATE_READY,
  TASK_STATE_SUSPEND,
  TASK_STATE_DONE,
  TASK_STATE_LENGTH
} task_state_type;

// Estrutura que define um Task Control Block (TCB)
typedef struct task_t
{
  struct task_t *prev, *next ;		// ponteiros para usar em filas
  int id ;				// identificador da tarefa
  ucontext_t context ;			// contexto armazenado da tarefa
  task_state_type status ;			// pronta, rodando, suspensa, ...
  int prio_static;   // prioridade estática (fixa, entre -20 e +20)
  int prio_dynamic;  // prioridade dinâmica (usada no escalonador)
  int quantum;       // quantum da tarefa
  bool is_user_task; // flag de tarefa de usuario
  unsigned int processor_time; // tempo de execucao da cpu
  unsigned int activations; // qtde de ativacoes de quantum
  int exit_code;            // codigo de encerramento da tarefa
  struct task_t *queue_task_suspend;   // fila de tarefas suspensas pela tarefa atual (quando a tarefa atual terminar, as tarefas suspensas devem voltar a ready)
  // ... (outros campos serão adicionados mais tarde)
} task_t ;

// estrutura que define um semáforo
typedef struct
{
  // preencher quando necessário
} semaphore_t ;

// estrutura que define um mutex
typedef struct
{
  // preencher quando necessário
} mutex_t ;

// estrutura que define uma barreira
typedef struct
{
  // preencher quando necessário
} barrier_t ;

// estrutura que define uma fila de mensagens
typedef struct
{
  // preencher quando necessário
} mqueue_t ;

#endif

