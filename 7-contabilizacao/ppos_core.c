#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/time.h>
#include "ppos_data.h"
#include "ppos.h"
#include "queue.h"

/* tamanho de pilha das tarefas */
#define __PPOS_CORE_STACKSIZE__ 64*1024

// Define o tipo de algoritmo de escalonaento que sera executado
#define __PPOS_SCHEDULER_ALGORITHM__() scheduler_in_envelhecimento()

// Define se o dispatcher vai chamar diretamente a funcao 
// que implementa o algoritmo de escalonamento
// 0 -> CHAMA UMA FUNCAO GENERICA NA QUAL ESTA CHAMA UMA FUNCAO DE ESCALOMANETO ESPECIFICO
// 1 -> CHAMA DIRETAMENTE UMA FUNCAO DE ESCALOMANETO ESPECIFICO
#define __PPOS_CHAMAR_IMPLEMENTACAO_SCHEDULER_DIRETAMENTE__ 1

// Define se as mensagens de ERRO aparecerao no terminal
// 0 -> SEM MENSAGENS DE DEBUG NO TERMINAL
// 1 -> COM MENSAGENS DE DEBUG NO TERMINAL
#define __PPOS_CORE_DEBUG__ 1

// Define se as mensages de DEBUG aparecerao no terminal
//#define DEBUG


// id das tarefas do sistema operacional
#define __PPOS_ID_MAIN__ 0
#define __PPOS_ID_DISPATCHER__ 1

// Definicao do valor default da quantum
#define __PPOS_DEFAULT_QUANTUM__ 10


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

static unsigned int ppos_core_systime;

// atribuidor de id de tarefas
static unsigned long ppos_core_id_task;

// fila de tarefas prontas do sistema
static unsigned long ppos_size_tasks_ready;

// fila de tarefas prontas do sistema
static task_t *ppos_core_queue_task_ready;

// tcb tarefa principal (main)
static task_t ppos_core_task_main;

// tcb tarefa dispacther
static task_t ppos_core_task_dispatcher;

// apontador para a tarefa atual do sistema
static task_t *ppos_core_task_current;

// estrutura que define um tratador de sinal (deve ser global ou static)
struct sigaction action ;

// estrutura de inicialização do timer
struct itimerval timer;

//----------------------------------------------------------------------------------------------------


//----------------------------------------------------------------------------------------------------
// private functions

static void ISR_timer_ticks(int signum);

static void timer_init();

static void dispatcher(void * arg);

#if __PPOS_CHAMAR_IMPLEMENTACAO_SCHEDULER_DIRETAMENTE__ == 0
static task_t *scheduler();
#endif

static int task_append_queue_ready(task_t *task);

static int task_remove_queue_ready(task_t *task);

static void task_finish(task_t *task);

static task_t *scheduler_in_fcs();

static task_t *scheduler_in_envelhecimento();

#ifdef DEBUG
static void print_queue_task (void *ptr);
#endif

#define __PPOS_SWITCH_CONTEXTO_TO_DISPACHER__() ppos_core_task_dispatcher.activations++; task_switch(&ppos_core_task_dispatcher)

//----------------------------------------------------------------------------------------------------

// funções gerais ==============================================================

// Inicializa o sistema operacional; deve ser chamada no inicio do main()
void ppos_init ()
{
    // inicilizacao das variaveis da classe
    ppos_core_systime = 0;
    ppos_core_id_task = 0;

    //
    ppos_size_tasks_ready = 0;
    ppos_core_queue_task_ready = NULL;

    //
    ppos_core_task_current = NULL;

    /* desativa o buffer da saida padrao (stdout), usado pela função printf para evitar condicoes de corrida no terminal*/
    setvbuf (stdout, 0, _IONBF, 0);

    // Adiciona a tarefa main() na fila de tarefas do sistema
    task_init(&ppos_core_task_main, NULL, NULL);

    // Adiciona a tarefa que executa as outras tarefas dispatcher() TAREFA DO SISTEMA
    task_init(&ppos_core_task_dispatcher, &dispatcher, NULL);
    ppos_core_task_dispatcher.is_user_task = false;

    // Inicializa o timer
    timer_init();

    // Faz o ponteiro da tarefa atual apontar para o endereco da tarefa main
    // isso eh feito pois essa funcao so eh chamada dentro do main()
    ppos_core_task_current = &ppos_core_task_main;
    ppos_core_task_current->activations++;
}


// gerência de tarefas =========================================================


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

    if(ppos_core_id_task > __PPOS_ID_MAIN__)
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

    // Adiciona e verifica se a tarefa foi adicionada na fila de tarefas prontas do sistema
    if(  task_append_queue_ready(task) < 0 )
    {
        __PPOS_CORE_STD_MSG_ERRO__("Erro ao adicionar a tarefa na fila de tarefas prontas do sistema");
        return -1;
    }

    // Atribui o id da tarefa
    task->id = ppos_core_id_task++;

    // Atribui prioridade default
    task->prio_static = task->prio_dynamic = 0;

    // Atribui quantum de inicializzacao
    task->quantum = -1;

    // Por default, todas as tarefas sao tarefas de usuario
    task->is_user_task = true;

    //
    task->processor_time = 0;

    //
    task->activations = 0;

    #ifdef DEBUG
    __PPOS_CORE_STD_DEBUG_MSG__("task_init: instalada tarefa %d (body function 0x%p)\n", task->id, start_func);
    #endif

    return task->id;
}

//
// Informa o identificador da tarefa corrente
// 
int task_id ()
{
    return ppos_core_task_current->id;
}


// Termina a tarefa corrente
// exit_code : código de término devolvido pela tarefa corrente (ignorar este parâmetro por enquanto,
// pois ele somente será usado mais tarde).
void task_exit (int exit_code)
{
    #ifdef DEBUG
    __PPOS_CORE_STD_DEBUG_MSG__("task_exit: tarefa %d exited, execution time %d ms, processor time %d ms, %d activations\n", 
                                ppos_core_task_current->id,
                                ppos_core_systime,
                                ppos_core_task_current->processor_time,
                                ppos_core_task_current->activations
                                );
    #endif

    ppos_core_task_current->status = TASK_STATE_DONE;

    if(ppos_core_task_current->id == __PPOS_ID_MAIN__)
    {
        task_finish(&ppos_core_task_main);
    }
    else if(ppos_core_task_current->id == __PPOS_ID_DISPATCHER__)
    {
        // Mata a tarefa dispatcher
        task_finish(&ppos_core_task_dispatcher);
        return;
    }

    //
    __PPOS_SWITCH_CONTEXTO_TO_DISPACHER__();
}  

// Transfere o processador para outra tarefa
// task: tarefa que irá assumir o processador
// retorno: valor negativo se houver erro, ou zero se ok
int task_switch (task_t *task)
{
    ucontext_t *addr_to_save;

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

    #ifdef DEBUG
    __PPOS_CORE_STD_DEBUG_MSG__("task_switch: trocando contexto %d -> %d\n", ppos_core_task_current->id, task->id);
    #endif

    // Atribui a tarefa atual do sistema na proxima troca de contexto
    ppos_core_task_current = task;

    // Salva o atual contexto em addr_to_save e restaura e pula para um outro contexto
    swapcontext(addr_to_save, &task->context);

    return 0;
}

// operações de escalonamento ==================================================


// a tarefa atual libera o processador para outra tarefa
//1. coloca a tarefa atual no fim da fila de prontas
//2. muda o estado da tarefa atual para PRONTA
//3. devolve a CPU ao despachante
void task_yield ()
{
    #ifdef DEBUG
    __PPOS_CORE_STD_DEBUG_MSG__("task_yield: tarefa %d yields the CPU\n", ppos_core_task_current->id);
    #endif

    //
    __PPOS_SWITCH_CONTEXTO_TO_DISPACHER__();
}


// define a prioridade estática de uma tarefa (ou a tarefa atual)
void task_setprio (task_t *task, int prio)
{
    if(prio < -20)
    {
        prio = -20;
    }
    else if(prio > 20)
    {
        prio = 20;
    }

    // Ajusta prioridade da tarefa atual
    if(task == NULL)
    {
        ppos_core_task_current->prio_static = ppos_core_task_current->prio_dynamic = prio;
        return;
    }
    
    task->prio_static = task->prio_dynamic = prio;
}

//
// retorna a prioridade estática de uma tarefa (ou a tarefa atual)
//
int task_getprio (task_t *task)
{
    return task != NULL ? task->prio_static : ppos_core_task_current->prio_static;
}

//
// suspende a tarefa corrente por t milissegundos
//
unsigned int systime ()
{
    return ppos_core_systime;
}



//----------------------------------------------------------------------------------------------------
// private functions implements

//
//
//
static void timer_init()
{
    // registra a ação para o sinal de timer SIGALRM (sinal do timer)
    action.sa_handler = &ISR_timer_ticks ;
    sigemptyset (&action.sa_mask) ;
    action.sa_flags = 0 ;
    if (sigaction (SIGALRM, &action, 0) < 0)
    {
        __PPOS_CORE_STD_MSG_ERRO__("Erro ao cadastrar o callback: ");
        exit (1) ;
    }

    // ajusta valores do temporizador
    timer.it_value.tv_usec =  1000;      // primeiro disparo, em micro-segundos
    timer.it_value.tv_sec  = 0 ;      // primeiro disparo, em segundos
    timer.it_interval.tv_usec = 1000 ;   // disparos subsequentes, em micro-segundos
    timer.it_interval.tv_sec  = 0 ;   // disparos subsequentes, em segundos

    // arma o temporizador ITIMER_REAL
    if (setitimer (ITIMER_REAL, &timer, 0) < 0)
    {
        __PPOS_CORE_STD_MSG_ERRO__("Erro ao armar o timer: ") ;
        exit (1) ;
    }
}

//
//
//
static void ISR_timer_ticks(int signum)
{
    // Atualiza variavel systime
    ppos_core_systime++;

    if( ! ppos_core_task_current->is_user_task )
        return;

    if(--ppos_core_task_current->quantum != 0)
        return;

    //
    ppos_core_task_current->status = TASK_STATE_SUSPEND;
    
    //
    task_remove_queue_ready(ppos_core_task_current);

    //
    __PPOS_SWITCH_CONTEXTO_TO_DISPACHER__();
}

//  
// Tarefa do sistema operacional responsavel por 
// executar e matar a tarefa de usuario selecionada pelo escalonador
static void dispatcher(void * arg)
{
    task_t *task_to_exec;
    unsigned int task_time_now;

    // retira o dispatcher da fila de prontas, pois o mesmo esta executando
    task_remove_queue_ready(&ppos_core_task_dispatcher);
    
    // enquanto houverem tarefas na fila do sistema
    while( ppos_size_tasks_ready )
    {
        #ifdef DEBUG
        __PPOS_CORE_STD_DEBUG_MSG__("dispatcher: lista de tarefas prontas");
        queue_print(" ", (queue_t *) ppos_core_queue_task_ready, print_queue_task);
        #endif

        // escolhe a próxima tarefa de usuario a executar
        #if __PPOS_CHAMAR_IMPLEMENTACAO_SCHEDULER_DIRETAMENTE__ == 1
        task_to_exec = __PPOS_SCHEDULER_ALGORITHM__();
        #else
        task_to_exec = scheduler();
        #endif

        // escalonador escolheu uma tarefa?
        if(task_to_exec != NULL)
        {
            // Verifica se a tarefa que sera executada eh uma tarefa de usuario
            // Se sim, entao
            if ( task_to_exec->is_user_task )
            {
                // Define o valor do quantum default para a tarefa
                task_to_exec->quantum = __PPOS_DEFAULT_QUANTUM__;
            }

            // Incrementa o numero de ativacoes da tarefa que sera executada
            task_to_exec->activations++;

            // Pega o tempo atual do sistema
            task_time_now = ppos_core_systime;

            // transfere controle da CPU para a próxima tarefa
            task_switch(task_to_exec);

            // Calcula o tempo que a tarefa usou a cpu
            task_to_exec->processor_time += (ppos_core_systime - task_time_now) ;

            // voltando ao dispatcher, trata a ultima tarefa executado pela CPU de acordo com seu estado
            switch (task_to_exec->status)
            {
                case TASK_STATE_READY:

                break;

                case TASK_STATE_RUNNING:
                break;

                case TASK_STATE_DONE:
                    // Como a tarefa que executou chamou task_exit(), entao a tarefa sera morta pelo sistema operacional 
                    task_finish(task_to_exec);
                break;

                case TASK_STATE_SUSPEND:
                    //
                    task_append_queue_ready(task_to_exec);
                break;

                default:
                break;
            }
        }
    }
    
    // encerra a tarefa dispatcher
    task_exit( 0 );
}


#if __PPOS_CHAMAR_IMPLEMENTACAO_SCHEDULER_DIRETAMENTE__ == 0
//
// Escolhe qual tarefa da fila de sistema sera executada
//
static task_t *scheduler()
{
    return __PPOS_SCHEDULER_ALGORITHM__();
}
#endif

//
// Mata a tarefa do sistema operacional
//
static void task_finish(task_t *task)
{
    // Retira a tarefa do sistema operacional
    task_remove_queue_ready(task);

    // Se nao for a tarefa main, pois a msm n tem alocacao dinamica
    // e a tarefa estiver com status de DONE entao libera a memoria alocada para a stack pointer
    if(task->id != __PPOS_ID_MAIN__ && task->status == TASK_STATE_DONE)
        free(task->context.uc_stack.ss_sp);

    #ifdef DEBUG
    __PPOS_CORE_STD_DEBUG_MSG__("task_finish: tarefa %d desinstalada\n", task->id);
    #endif
}


//
// Adiciona tarefa na fila de tarefas prontas do sistema
//
static int task_append_queue_ready(task_t *task)
{
    int ret = queue_append((queue_t **) &ppos_core_queue_task_ready, (queue_t* ) task);

    if(ret == 0) ppos_size_tasks_ready++;

    return ret;
}

//
// Remove a tarefa da fila de tarefas prontas do sistema
//
static int task_remove_queue_ready(task_t *task)
{
    int ret = queue_remove((queue_t**) &ppos_core_queue_task_ready, (queue_t* ) task);

    if(ret == 0) ppos_size_tasks_ready--;

    return ret;
}


//
// escholher a primeira tarefa que estiver pronto para execucao,
//
static task_t *scheduler_in_fcs()
{
    // Guarda o endereco do primeiro elemento da fila
    task_t *ret = ppos_core_queue_task_ready;

    // Retira o primeiro elemento da fila
    task_remove_queue_ready(ppos_core_queue_task_ready);

    // Coloca novamente o primeiro elemento na fila
    task_append_queue_ready(ret);

    return ret;
}

//
//
//
static task_t *scheduler_in_envelhecimento()
{
    task_t *task_selected;
    task_t *task;
    int count_tasks;

    task = task_selected = ppos_core_queue_task_ready;
    count_tasks = 0;

    // Escolhe a tarefa com mais prioridade
    while(count_tasks++ < ppos_size_tasks_ready)
    {
        // Verifica se sao tarefas distintas
        if(task->id != task_selected->id)
        {
            // Verifica se a task atual possui mais prioridade dinamica que a task selecionada
            if (task->prio_dynamic < task_selected->prio_dynamic)
            {
                if (--task_selected->prio_dynamic < -20) task_selected->prio_dynamic = -20;
                
                task_selected = task;
            }
            else
            {
                // Limita a prioridade dinâmica ao mínimo de -20
                if (--task->prio_dynamic < -20) task->prio_dynamic = -20;
            }
        }

        task = task->next;
    }

    // Reseta a prioridade dinâmica da tarefa escolhida para execução
    task_selected->prio_dynamic = task_selected->prio_static;

    return task_selected;
}


#ifdef DEBUG
static void print_queue_task (void *ptr)
{
    task_t *task = (task_t*) ptr;

    if(task == NULL)
        return;

    printf ("%d", task->id) ;
}
#endif

