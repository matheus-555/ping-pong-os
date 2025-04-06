#include <stdio.h>
#include <stdlib.h>
#include "ppos.h"
#include "queue.h"

#define QTDE_PRODUTORES 3
#define QTDE_CONSUMIDORES 2
#define BUFFER_LENGTH 5
#define NAME_LENGTH 2+1

void consumidor(void *arg);
void produtor(void *arg);

task_t produtores[QTDE_PRODUTORES];
task_t consumidores[QTDE_CONSUMIDORES];

int item;
int buffer[BUFFER_LENGTH] = {-1,};
int i_append = 0, i_remove = 0;

semaphore_t s_buffer, s_item, s_vaga;

int main(int argc, char const *argv[])
{
    int i;
    char name[QTDE_PRODUTORES+QTDE_CONSUMIDORES][NAME_LENGTH];

    ppos_init ();
    sem_init(&s_buffer, 1);
    sem_init(&s_item, 0);
    sem_init(&s_vaga, BUFFER_LENGTH);

    for(i = 0; i < QTDE_PRODUTORES; i++)
    {
        snprintf(name[i], NAME_LENGTH, "p%d", i+1);
        task_init(&produtores[i], produtor, name[i]);
    }
        
    for(i = 0; i < QTDE_CONSUMIDORES; i++)
    {
        snprintf(name[QTDE_PRODUTORES+i], NAME_LENGTH, "c%d", i+1);
        task_init(&consumidores[i], consumidor, name[QTDE_PRODUTORES+i]);
    }

    task_exit(0);

    return 0;
}



void consumidor(void *arg)
{
    char *name = (char *) arg;
    int elem;

    while(1)
    {
        sem_down(&s_item);
        sem_down(&s_buffer);
        // retira item do buffer
        elem = buffer[i_remove];
        buffer[i_remove++] = -1;
        if(i_remove == BUFFER_LENGTH) i_remove = 0;
        sem_up(&s_buffer);
        sem_up(&s_vaga);
        printf("\t\t%s consumiu %d\n", name, elem);
        task_sleep(1000);
    }
}


void produtor(void *arg)
{
    char *name = (char *) arg;

    while(1)
    {
        task_sleep(1000);
        // Usa o tempo atual como semente para a geração de números aleatórios
        sem_down(&s_vaga);
        sem_down(&s_buffer);
        item = rand() % 99;
        // insere item no buffer
        buffer[i_append++] = item;
        if(i_append == BUFFER_LENGTH) i_append = 0;
        sem_up(&s_buffer);
        sem_up(&s_item);
        printf("%s produziu %d\n", name, item);
    }
}