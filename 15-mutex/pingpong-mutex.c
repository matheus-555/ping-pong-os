#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ppos.h"
#include "queue.h"


#define COM_MUTEX 1


void imprime_na_tela(const char *msg);
void imprime_sao_bernardo_do_campo();
void imprime_diadema();
void imprime_santo_andre();
void imprime_sao_caetano_do_sul();


task_t imprime_sbc;
task_t imprime_scs;
task_t imprime_dia;
task_t imprime_san;
mutex_t mutex;

int main(int argc, char const *argv[])
{
    ppos_init();

    #if COM_MUTEX == 1
    mutex_init(&mutex);
    #endif

    task_init(&imprime_sbc, &imprime_sao_bernardo_do_campo, NULL);
    task_init(&imprime_scs, &imprime_sao_caetano_do_sul,    NULL);
    task_init(&imprime_dia, &imprime_diadema,               NULL);
    task_init(&imprime_san, &imprime_santo_andre,           NULL);

    task_wait(&imprime_dia);
    task_wait(&imprime_san);
    task_wait(&imprime_sbc);
    task_wait(&imprime_scs);

    task_exit(0);

    return 0;
}


void imprime_na_tela(const char *msg)
{
    if (msg == NULL)
        return;

    #if COM_MUTEX == 1
    mutex_lock(&mutex);
    #endif

    for(int i = 0; i < strlen(msg); i++)
    {
        printf("%c", msg[i]);
        task_sleep(100);
    }

    #if COM_MUTEX == 1
    mutex_unlock(&mutex);
    #endif
}

void imprime_sao_bernardo_do_campo()
{
    const char *msg = "SAO BERNARDO DO CAMPO\n";

    imprime_na_tela(msg);

    task_exit(0);
}

void imprime_diadema()
{
    const char *msg = "DIADEMA\n";

    imprime_na_tela(msg);

    task_exit(0);
}

void imprime_santo_andre()
{
    const char *msg = "SANTO ANDRE\n";

    imprime_na_tela(msg);

    task_exit(0);
}


void imprime_sao_caetano_do_sul()
{
    const char *msg = "SAO CAETANO DO SUL\n";

    imprime_na_tela(msg);

    task_exit(0);
}

