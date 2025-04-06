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
typedef enum {
    PPOS_CORE_ID_SYSTEM_TASK_MAIN = 0,
    PPOS_CORE_ID_SYSTEM_TASK_DISPATCHER,
    PPOS_CORE_ID_SYSTEM_TASK_CONTROL_SLEEP,
    PPOS_CORE_ID_SYSTEM_TASK_LENGTH
} pppos_core_id_system_tasks_enum;


// Definicao do valor default da quantum
#define __PPOS_DEFAULT_QUANTUM__ 10


#ifdef DEBUG_1
#define __PPOS_CORE_STD_DEBUG_MSG__(...) printf("# DEBUG in PPOS_CORE.c -> "); printf(__VA_ARGS__)
#endif

#if __PPOS_CORE_DEBUG__
#define __PPOS_CORE_STD_MSG_ERRO__(msg) fprintf(stderr, "### ERRO in PPOS_CORE.c:%d" ": " msg "\n", __LINE__)
#else
#define __PPOS_CORE_STD_MSG_ERRO__(msg)
#endif


//----------------------------------------------------------------------------------------------------
// private variables

// variavel que contabiliza o tempo do sistema em ms
static unsigned int ppos_core_systime;

// atribuidor de id de tarefas
static unsigned long ppos_core_id_task;

// fila de tarefas prontas do sistema
static unsigned long ppos_size_tasks_ready;

// fila de tarefas que estao dormindo
static unsigned long ppos_size_tasks_sleep;

// fila de tarefas prontas do sistema
static task_t *ppos_core_queue_task_ready;

// fila de tarefas dormindo
static task_t *ppos_core_queue_task_sleep;

// tcb tarefa principal (main)
static task_t ppos_core_task_main;

// tcb tarefa dispacther
static task_t ppos_core_task_dispatcher;

// tcb tarefa que controla as tarefas adormecidas
static task_t ppos_core_task_control_sleep_tasks;

// apontador para a tarefa atual do sistema
static task_t *ppos_core_task_current;

// estrutura que define um tratador de sinal (deve ser global ou static)
static struct sigaction action;

// estrutura de inicialização do timer
static struct itimerval timer;

// controle da preempcao das tarefas
static char ppos_core_enable_preemp;

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

static void control_sleep_tasks(void *arg);

#ifdef DEBUG
static void print_queue_task (void *ptr);
#endif

#define __PPOS_SWITCH_CONTEXTO_TO_DISPATCHER__() ppos_core_task_dispatcher.activations++; task_switch(&ppos_core_task_dispatcher)

// ---- Operacoes de atribuicao de valores de forma atomica
//
#define __PPOS_DISABLE_PREEMP() __sync_fetch_and_and(&ppos_core_enable_preemp, 0)

//
#define __PPOS_ENABLE_PREEMP() __sync_fetch_and_or(&ppos_core_enable_preemp, 1)

//----------------------------------------------------------------------------------------------------

// funções gerais ==============================================================

// Inicializa o sistema operacional; deve ser chamada no inicio do main()
void ppos_init ()
{
    // inicilizacao das variaveis da classe
    __PPOS_DISABLE_PREEMP();
    ppos_core_systime = 0;
    ppos_core_id_task = 0;

    //
    ppos_size_tasks_ready = 0;
    ppos_core_queue_task_ready = NULL;

    //
    ppos_size_tasks_sleep = 0;
    ppos_core_queue_task_sleep = NULL;

    //
    ppos_core_task_current = NULL;

    /* desativa o buffer da saida padrao (stdout), usado pela função printf para evitar condicoes de corrida no terminal*/
    setvbuf (stdout, 0, _IONBF, 0);

    // Adiciona a tarefa main() na fila de tarefas do sistema
    task_init(&ppos_core_task_main, NULL, NULL);

    // Adiciona a tarefa que executa as outras tarefas dispatcher() TAREFA DO SISTEMA
    task_init(&ppos_core_task_dispatcher, &dispatcher, NULL);
    ppos_core_task_dispatcher.is_user_task = false;

    // adiciona a tarefa que controla as tarefas que estao dormindo TAREFA DO SISTEMA
    task_init(&ppos_core_task_control_sleep_tasks, &control_sleep_tasks, NULL);
    ppos_core_task_control_sleep_tasks.is_user_task = false;

    // Inicializa o timer
    timer_init();
    __PPOS_ENABLE_PREEMP();

    // Faz o ponteiro da tarefa atual apontar para o endereco da tarefa main
    // isso eh feito pois essa funcao so eh chamada dentro do main()
    ppos_core_task_current = &ppos_core_task_main;
    ppos_core_task_current->activations++;

    __PPOS_SWITCH_CONTEXTO_TO_DISPATCHER__();
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

    if(ppos_core_id_task > PPOS_CORE_ID_SYSTEM_TASK_MAIN)
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

    //
    task->exit_code = -1;

    //
    task->queue_task_suspend = NULL;

    //
    task->status = TASK_STATE_SUSPEND;

    //
    task->time_to_awake = 0;

    // Adiciona e verifica se a tarefa foi adicionada na fila de tarefas prontas do sistema
    if(  task_append_queue_ready(task) < 0 )
    {
        __PPOS_CORE_STD_MSG_ERRO__("Erro ao adicionar a tarefa na fila de tarefas prontas do sistema");
        return -1;
    }

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
    #ifdef DEBUG_1
    __PPOS_CORE_STD_DEBUG_MSG__("task_exit: tarefa %d exited, execution time %d ms, processor time %d ms, %d activations\n", 
                                ppos_core_task_current->id,
                                ppos_core_systime,
                                ppos_core_task_current->processor_time,
                                ppos_core_task_current->activations
                                );
    #endif

    //
    ppos_core_task_current->status = TASK_STATE_DONE;

    //
    ppos_core_task_current->exit_code = exit_code;

    // verifica se a tarefa atual eh uma tarefa do sistema
    if( ! ppos_core_task_current->is_user_task )
    {
        // Mata as tarefas do sistema
        task_finish(ppos_core_task_current);

        // 
        if ( ppos_core_task_current->id == PPOS_CORE_ID_SYSTEM_TASK_DISPATCHER )
            return;
    }

    //
    __PPOS_SWITCH_CONTEXTO_TO_DISPATCHER__();
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

    #ifdef DEBUG_2
    __PPOS_CORE_STD_DEBUG_MSG__("task_switch: trocando contexto %d -> %d\n", ppos_core_task_current->id, task->id);
    #endif

    // Atribui a tarefa atual do sistema na proxima troca de contexto
    ppos_core_task_current = task;

    // Salva o atual contexto em addr_to_save e restaura e pula para um outro contexto
    swapcontext(addr_to_save, &task->context);

    return 0;
}


// suspende a tarefa atual,
// transferindo-a da fila de prontas para a fila "queue"
void task_suspend (task_t **queue)
{
    if(queue == NULL)
        return;

    // Verifica se a tarefa atual esta ready,
    // se sim, entao retire a tarefa da fila de ready
    if (ppos_core_task_current->status == TASK_STATE_READY)
        task_remove_queue_ready(ppos_core_task_current);

    // muda o status da tafera
    ppos_core_task_current->status = TASK_STATE_SUSPEND;

    // adiciona a tarefa atual na fila de tarefas
    queue_append((queue_t **) queue, (queue_t *) ppos_core_task_current);
}

// acorda a tarefa indicada,
// trasferindo-a da fila "queue" para a fila de prontas
void task_awake (task_t *task, task_t **queue)
{
    // remove a tarefa suspensa da fila de tarefas suspensas 
    queue_remove((queue_t **) queue, (queue_t *) task);

    // coloca a tarefa que saiu da lista de suspensa na lista de ready
    task_append_queue_ready(task);
}

// operações de escalonamento ==================================================


// a tarefa atual libera o processador para outra tarefa
//1. coloca a tarefa atual no fim da fila de prontas
//2. muda o estado da tarefa atual para PRONTA
//3. devolve a CPU ao despachante
void task_yield ()
{
    #ifdef DEBUG_2
    __PPOS_CORE_STD_DEBUG_MSG__("task_yield: tarefa %d yields the CPU\n", ppos_core_task_current->id);
    #endif

    //
    __PPOS_SWITCH_CONTEXTO_TO_DISPATCHER__();
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

//
// suspende a tarefa corrente por t milissegundos
//
void task_sleep (int t) 
{
    //
    task_suspend(&ppos_core_queue_task_sleep);
    ppos_size_tasks_sleep++;

    //
    ppos_core_task_current->time_to_awake = ppos_core_systime + t;

    //
    task_append_queue_ready(&ppos_core_task_control_sleep_tasks);
    
    __PPOS_SWITCH_CONTEXTO_TO_DISPATCHER__();
}


// operações de sincronização ==================================================

// a tarefa corrente aguarda o encerramento de outra task
int task_wait (task_t *task)
{
    if (task == NULL)
        return -1;

    if (task->status == TASK_STATE_DONE)
        return -1;

    //
    task_suspend(&task->queue_task_suspend);

    __PPOS_SWITCH_CONTEXTO_TO_DISPATCHER__();

    return task->exit_code;
}

//
// inicializa um semáforo com valor inicial "value"
//
int sem_init (semaphore_t *s, int value) 
{
    if(s == NULL)
    {
        __PPOS_CORE_STD_MSG_ERRO__("Semaforo NULL");
        return -1;
    }

    // valor do contador inicial do semaforo
    s->count = value;

    // fila vazia para o semaforo
    s->queue = NULL;

    return 0;
}

//
// requisita o semáforo
//
int sem_down (semaphore_t *s) 
{
    if(s == NULL)
    {
        __PPOS_CORE_STD_MSG_ERRO__("Semaforo NULL");
        return -1;
    }

    //
    __PPOS_DISABLE_PREEMP();
    
    // se o contador do semaforo eh negativo
    if ( --s->count < 0 )
    {
        // suspende a tarefa atual
        task_suspend(&s->queue);

        //
        __PPOS_ENABLE_PREEMP();

        // volta ao dispatcher
        __PPOS_SWITCH_CONTEXTO_TO_DISPATCHER__();
    }

    //
    __PPOS_ENABLE_PREEMP();

    return 0;
}

//
// libera o semáforo
//
int sem_up (semaphore_t *s) 
{
    task_t *task_aux;

    if(s == NULL)
    {
        __PPOS_CORE_STD_MSG_ERRO__("Semaforo NULL");
        return -1;
    }

    // 
    __PPOS_DISABLE_PREEMP();

    // se houver tarefas suspensas
    if( ++s->count <= 0 )
    {
        // aponta para a primeira tarefa da fila
        task_aux = s->queue;

        // remove a primeira tarefa da fila
        queue_remove((queue_t **) &s->queue, (queue_t *) s->queue);

        // adiciona a tarefa removida da fila do semaforo na fila de tarefas prontas
        task_append_queue_ready(task_aux);
    }

    // 
    __PPOS_ENABLE_PREEMP();
    
    return 0;
}

//
// "destroi" o semáforo, liberando as tarefas bloqueadas
//
int sem_destroy (semaphore_t *s) 
{
    if(s == NULL)
    {
        __PPOS_CORE_STD_MSG_ERRO__("Semaforo NULL");
        return -1;
    }

    __PPOS_DISABLE_PREEMP();

    // acorda todas as tarefas suspensas pelo semaforo
    while(s->queue != NULL) { task_awake(s->queue, &s->queue); }

    __PPOS_ENABLE_PREEMP();

    return 0;
}



//
// inicializa uma barreira para N tarefas
//
int barrier_init (barrier_t *b, int N)
{
    if(b == NULL)
        return -1;

    if(N < 1)
        return -1;

    b->count_to_awake = N;
    b->queue = NULL;

    return 0;
}

//
// espera na barreira
//
int barrier_wait (barrier_t *b)
{
    

    if(b == NULL)
        return -1;

    // decrementa e verifica se ainda ha vaga na barreira
    if(--b->count_to_awake > 0)
    {
        // adiciona tarefa atual na fila de tarefas suspensas da barreira
        task_suspend(&b->queue);

        task_switch(&ppos_core_task_dispatcher);
    }

    __PPOS_DISABLE_PREEMP();

    // se nao ha vaga na barreira entao coloca as tarefas na fila de prontas
    while(b->queue != NULL) task_awake(b->queue, &b->queue);

    __PPOS_ENABLE_PREEMP();

    return 0;
}

//
// destrói a barreira, liberando as tarefas
//
int barrier_destroy (barrier_t *b)
{
    if(b == NULL)
        return -1;

    __PPOS_DISABLE_PREEMP();

    while(b->queue != NULL) task_awake(b->queue, &b->queue);

    __PPOS_ENABLE_PREEMP();

    return 0;
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

    if( ! ppos_core_enable_preemp || ! ppos_core_task_current->is_user_task )
        return;

    if( --ppos_core_task_current->quantum != 0 )
        return;

    //
    __PPOS_SWITCH_CONTEXTO_TO_DISPATCHER__();
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

    // 
    task_remove_queue_ready(&ppos_core_task_control_sleep_tasks);
    
    // enquanto houverem tarefas na fila do sistema
    while( ppos_size_tasks_ready )
    {
        #ifdef DEBUG_2
        __PPOS_CORE_STD_DEBUG_MSG__("dispatcher: lista de tarefas prontas");
        queue_print(" ", (queue_t *) ppos_core_queue_task_ready, print_queue_task);
        #endif

        #ifdef DEBUG_2
        __PPOS_CORE_STD_DEBUG_MSG__("dispatcher: lista de tarefas dormindo");
        queue_print(" ", (queue_t *) ppos_core_queue_task_sleep, print_queue_task);
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
                case TASK_STATE_RUNNING:
                break;

                case TASK_STATE_READY:
                break;

                case TASK_STATE_SUSPEND:
                break;

                case TASK_STATE_DONE:
                    // Como a tarefa que executou chamou task_exit(), entao a tarefa sera morta pelo sistema operacional 
                    task_finish(task_to_exec);
                    // Acorda tarefas que foram suspendidas esperando a tarefa atual terminar
                    while (task_to_exec->queue_task_suspend != NULL) { task_awake(task_to_exec->queue_task_suspend, &task_to_exec->queue_task_suspend); }
                break;

                default:
                break;
            }
        }
    }

    //
    task_switch(&ppos_core_task_control_sleep_tasks);

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
    if(task->id != PPOS_CORE_ID_SYSTEM_TASK_MAIN && task->status == TASK_STATE_DONE)
        free(task->context.uc_stack.ss_sp);

    #ifdef DEBUG_2
    __PPOS_CORE_STD_DEBUG_MSG__("task_finish: tarefa %d desinstalada\n", task->id);
    #endif
}


//
// Adiciona tarefa na fila de tarefas prontas do sistema
//
static int task_append_queue_ready(task_t *task)
{
    int ret = queue_append((queue_t **) &ppos_core_queue_task_ready, (queue_t* ) task);

    if(ret == 0) { ppos_size_tasks_ready++; task->status = TASK_STATE_READY; }

    return ret;
}

//
// Remove a tarefa da fila de tarefas prontas do sistema
//
static int task_remove_queue_ready(task_t *task)
{
    int ret = queue_remove((queue_t**) &ppos_core_queue_task_ready, (queue_t* ) task);

    if(ret == 0) { ppos_size_tasks_ready--; }

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

static void control_sleep_tasks(void *arg)
{
    task_t *task_aux;
    int i;

    // enquanto houver tarefas na fila de tarefas dormindo
    while ( ppos_size_tasks_sleep )
    {
        // varre a fila de tarefas dormindo
        for (i = 0, task_aux = ppos_core_queue_task_sleep; i < ppos_size_tasks_sleep; i++)
        {
            // verifica se o tempo de acordar chegou
            if (task_aux->time_to_awake <= ppos_core_systime)
            {
                // remove a tarefa da fila de dormindo
                task_awake(task_aux, &ppos_core_queue_task_sleep);
                --ppos_size_tasks_sleep;

                // troca o apontador para a nova cabeca da fila
                task_aux = ppos_core_queue_task_sleep;
            }
            else
            {
                task_aux = task_aux->next;
            }
        }

        // se nao houver tarefas suspensas, entao tira esta tarefa da fila de prontas do sistena
        if ( ppos_size_tasks_sleep == 0 )
            task_remove_queue_ready(&ppos_core_task_control_sleep_tasks);

        task_switch(&ppos_core_task_dispatcher);
    }

    task_exit( 0 );
}


#ifdef DEBUG_2
static void print_queue_task (void *ptr)
{
    task_t *task = (task_t*) ptr;

    if(task == NULL)
        return;

    printf ("%d", task->id) ;
}
#endif

