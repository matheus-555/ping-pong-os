#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "ppos_data.h"
#include "ppos.h"
#include "queue.h"

/* tamanho de pilha das tarefas */
#define __PPOS_CORE_STACKSIZE__ 64*1024


#define __PPOS_ID_MAIN__ 0

#define __PPOS_CORE_DEBUG__ 0
#define DEBUG 1

#ifdef DEBUG
#define __PPOS_CORE_STD_DEBUG_MSG__(...) printf("# DEBUG in PPOS_CORE.c -> "); printf(__VA_ARGS__)
#endif

#if __PPOS_CORE_DEBUG__
#define __PPOS_CORE_STD_MSG_ERRO__(msg) fprintf(stderr, "### ERRO in PPOS_CORE.c:%d" ": " msg "\n", __LINE__)
#else
#define __PPOS_CORE_STD_MSG_ERRO__(msg)
#endif


//----------------------------------------------------------------------------------------------------
// private variables

// atribuidor de id de tarefas
static int ppos_core_id_task;

// fila de tarefas do sistema
static task_t *ppos_core_queue_task;

// apontador para a tarefa principal (main)
static task_t ppos_core_task_main;

// apontador para a tarefa atual do sistema
static task_t *ppos_core_task_current;

//----------------------------------------------------------------------------------------------------


// Inicializa o sistema operacional; deve ser chamada no inicio do main()
void ppos_init ()
{
    // inicilizacao das variaveis da classe
    ppos_core_id_task = 0;
    ppos_core_queue_task = NULL;
    ppos_core_task_current = NULL;

    /* desativa o buffer da saida padrao (stdout), usado pela função printf para evitar condicoes de corrida no terminal*/
    setvbuf (stdout, 0, _IONBF, 0);

    // Adiciona a tarefa main() na fila de tarefas do sistema
    //ppos_core_task_main.id = ppos_core_id_task;
    task_init(&ppos_core_task_main, NULL, NULL);

    // Faz o ponteiro da tarefa atual apontar para o endereco da tarefa main
    // isso eh feito pois essa funcao so eh chamada dentro do main()
    ppos_core_task_current = &ppos_core_task_main;
}

// Inicializa uma nova tarefa.
// task: estrutura que referencia a tarefa a ser iniciada
// start_routine: função que será executada pela tarefa
// arg: parâmetro a passar para a tarefa que está sendo iniciada
// retorno: o ID (>0) da nova tarefa ou um valor negativo, se houver erro
int task_init (task_t *task, void (*start_func)(void *), void *arg) 
{
    // pilha de memoria para a nova tarefa
    char *stack;

    // Verifica se o endereco que task aponta eh null
    if(task == NULL)
    {
        __PPOS_CORE_STD_MSG_ERRO__("Endereco de task eh null");
        return -1;
    }

    // Verifica se o endereco da funcao eh null AND se NAO eh a primeira instalacao de tarefa no sistema
    if(start_func == NULL && ppos_core_id_task > 0)
    {
        __PPOS_CORE_STD_MSG_ERRO__("Endereco da funcao eh null");
        return -1;
    }

    // Salva o contexto dentro do campo context da estrutura passada como parametro
    getcontext(&task->context);

    if(ppos_core_id_task != __PPOS_ID_MAIN__)
    {
        // Aloca uma pilha de memoria para a nova tarefa
        stack = (char*) malloc(__PPOS_CORE_STACKSIZE__);

        // Verifica se foi alocado memoria
        if(stack == NULL)
        {
            __PPOS_CORE_STD_MSG_ERRO__("Erro ao alocar memoria para a pilha da tarefa");
            return -1;
        }

        // Atribui o endereco da pilha alocada para o stack pointer da tarefa criada
        task->context.uc_stack.ss_sp = stack;
        
        // Atribui o tamanho da pilha criada para a tarefa criada
        task->context.uc_stack.ss_size = __PPOS_CORE_STACKSIZE__;

        // Atribui algumas flags para a tarefa criada
        task->context.uc_stack.ss_flags = 0;

        //
        task->context.uc_link = 0;

        // Ajusta alguns valores internos do contexto salvo em task->context
        // task->context: o contexto da tarefa 
        // start_func: a funcao que sera executada
        // 1: a quantidade de argumentos que sera passado para a funcao que sera executada
        // arg: argumento que sera passado para a funcao que sera executada
        makecontext(&task->context, (void*)(*start_func), 1, arg);
    }

    // Atribui o id da tarefa
    task->id = ppos_core_id_task++;

    // Adiciona e verifica se a tarefa foi adicionada na fila do sistema
    if( queue_append( (queue_t **) &ppos_core_queue_task, (queue_t *) task ) < 0 )
    {
        __PPOS_CORE_STD_MSG_ERRO__("Erro ao adicionar a tarefa na fila do sistema");
        return -1;
    }

    #ifdef DEBUG
    __PPOS_CORE_STD_DEBUG_MSG__("task_init: iniciada tarefa %d (body function 0x%p)\n", task->id, start_func);
    #endif

    return task->id;
}

// Transfere o processador para outra tarefa
// task: tarefa que irá assumir o processador
// retorno: valor negativo se houver erro, ou zero se ok
int task_switch (task_t *task)
{
    ucontext_t *addr_to_save, *addr_to_restore;

    // Verifica se o endereco que task aponta eh null
    if(task == NULL)
    {
        __PPOS_CORE_STD_MSG_ERRO__("Endereco de task eh null");
        return -1;
    }

    // Verifica se o id da tarefa NAO esta dentro do intevalo da variavel aribuidora de id de tarefa
    if(task->id > ppos_core_id_task)
    {
        __PPOS_CORE_STD_MSG_ERRO__("Id da task fora do intervlo dos ids das tarefas");
        return -1;
    }

    addr_to_save = &ppos_core_task_current->context;
    addr_to_restore = &task->context;

    #ifdef DEBUG
    __PPOS_CORE_STD_DEBUG_MSG__("task_switch: trocando contexto %d -> %d\n", ppos_core_task_current->id, task->id);
    #endif

    // Atribui a tarefa atual do sistema na proxima troca de contexto
    ppos_core_task_current = task;

    // Salva o atual contexto em addr_to_save e restaura e pula para um outro contexto
    swapcontext(addr_to_save, addr_to_restore);

    return 0;
}

// Termina a tarefa corrente
// exit_code : código de término devolvido pela tarefa corrente (ignorar este parâmetro por enquanto,
// pois ele somente será usado mais tarde).
void task_exit (int exit_code)
{
    #ifdef DEBUG
    __PPOS_CORE_STD_DEBUG_MSG__("task_exit: tarefa %d exited\n", ppos_core_task_current->id);
    #endif

    if(ppos_core_task_current->id != __PPOS_ID_MAIN__)
    {
        task_switch(&ppos_core_task_main);
    }
}

// Informa o identificador da tarefa corrente
int task_id ()
{
    return ppos_core_task_current->id;
}

