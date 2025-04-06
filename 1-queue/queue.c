#include <stdio.h>
#include <stdbool.h>
#include "queue.h"

#define __QUEUE_PERCORRE_FILA_EM_AMBOS_OS_SENTIDOS__  0
#define __QUEUE_PERCORRE_FILA_DIREITA_PARA_ESQUERDA__ 1
#define __QUEUE_PERCORRE_FILA_ESQUERDA_PARA_DIREITA__ 2

// Habilita ou nao as mensagens de debug dessa classe
// 0 -> Desabilita mensagens de debug
// 1 -> Habilita mensagnes de debug
#define __QUEUE_DEBUG__ 0

// Define a implementacao do tipo da busca de elementos na fila
// __QUEUE_PERCORRE_FILA_EM_AMBOS_OS_SENTIDOS__
// __QUEUE_PERCORRE_FILA_DIREITA_PARA_ESQUERDA__
// __QUEUE_PERCORRE_FILA_ESQUERDA_PARA_DIREITA__
#define __QUEUE_TIPO_DE_BUSCA_NA_FILA__ __QUEUE_PERCORRE_FILA_EM_AMBOS_OS_SENTIDOS__

#if __QUEUE_DEBUG__
#define __QUEUE_STD_MSG_ERRO__(msg) fprintf(stderr, "### ERRO in queue.c:%d" ": " msg "\n", __LINE__)
#else
#define __QUEUE_STD_MSG_ERRO__(msg)
#endif

// Private functions
static bool queue_is_element_exists (queue_t **queue, queue_t *elem);

// APARENTA ESTAR OK
//------------------------------------------------------------------------------
// Conta o numero de elementos na fila
// Retorno: numero de elementos na fila
int queue_size (queue_t *queue)
{
    int size = 0;
    queue_t *first_element;
    
    if (queue == NULL)
    {
        __QUEUE_STD_MSG_ERRO__("Endereco do ponteiro da fila eh null");
        return 0;
    }
        
    first_element = queue;

    do 
    {
        queue = queue->next;
        size++;
    } while (queue != first_element);

    return size;
}


// OK
//------------------------------------------------------------------------------
// Percorre a fila e imprime na tela seu conteúdo. A impressão de cada
// elemento é feita por uma função externa, definida pelo programa que
// usa a biblioteca. Essa função deve ter o seguinte protótipo:
//
// void print_elem (void *ptr) ; // ptr aponta para o elemento a imprimir
void queue_print (char *name, queue_t *queue, void print_elem (void*) )
{
    queue_t *first_element;

    if (name != NULL)
    {
        printf("%s: ", name);
    }

    if (queue == NULL)
    {
        printf("[]\n");
        return;
    }

    first_element = queue;
    printf("[");
    do 
    {
        print_elem(queue);
        printf(" ");
        queue = queue->next;
    } while (queue != first_element);
    printf("]\n");
}


// OK
//------------------------------------------------------------------------------
// Insere um elemento no final da fila.
// Condicoes a verificar, gerando msgs de erro:
// - a fila deve existir
// - o elemento deve existir
// - o elemento nao deve estar em outra fila
// Retorno: 0 se sucesso, <0 se ocorreu algum erro
int queue_append (queue_t **queue, queue_t *elem)
{
    queue_t *last;

    if (queue == NULL)
    {
        __QUEUE_STD_MSG_ERRO__("(queue_append) Endereco do ponteiro da fila eh null");
        return -1;
    }

    if (elem == NULL)
    {
        __QUEUE_STD_MSG_ERRO__("Endereco do elemento eh null");
        return -1;
    }

    if (elem->next != NULL || elem->prev != NULL)
    {
        __QUEUE_STD_MSG_ERRO__("Elemento esta em outra fila");
        return -1;
    }

    // Verifica se a fila esta vazia, se sim adiciona o primeiro elemento na fila
    if (*queue == NULL)
    {
        // Faz o primeiro elemento da fila apontar para si mesmo
        elem->next = elem->prev = elem;
        // O ponteiro da fila passa a apontar para esse elemento
        *queue = elem;
    }
    else
    {
        // Obtem o ultimo elemento da fila
        last = (*queue)->prev;
        last->next = elem;
        elem->prev = last;
        elem->next = *queue;
        (*queue)->prev = elem;
    }
    
    return 0;
}

// OK
//------------------------------------------------------------------------------
// Remove o elemento indicado da fila, sem o destruir.
// Condicoes a verificar, gerando msgs de erro:
// - a fila deve existir
// - a fila nao deve estar vazia
// - o elemento deve existir
// - o elemento deve pertencer a fila indicada
// Retorno: 0 se sucesso, <0 se ocorreu algum erro
int queue_remove (queue_t **queue, queue_t *elem)
{
    if(!queue_is_element_exists(queue, elem))
        return -1;

    // Verifica se existe mais de um elemento na fila
    if(elem != elem->next)
    {
        // Define as novas ligacoes da lista
        elem->next->prev = elem->prev;
        elem->prev->next = elem->next;
    }

    // Desfaz as ligacoes do elemento retirado da lista
    elem->next = elem->prev = NULL;
    
    return 0;
}


// OK
//------------------------------------------------------------------------------
// Verifica se o elemento passado se encontra dentro da fila passada e
// realiza as mudancas necessarias no ponteiro da fila.
// - a fila deve existir
// - a fila nao deve estar vazia
// - o elemento deve existir
// - o elemento deve pertencer a fila indicada
// Retorno: true se sucesso, false se ocorreu algum erro ou o elemento nao foi encontrado
static bool queue_is_element_exists (queue_t **queue, queue_t *elem)
{
    #if __QUEUE_TIPO_DE_BUSCA_NA_FILA__ == __QUEUE_PERCORRE_FILA_EM_AMBOS_OS_SENTIDOS__
    queue_t *elem_remove_to_right, *elem_remove_to_left;
    #elif __QUEUE_TIPO_DE_BUSCA_NA_FILA__ == __QUEUE_PERCORRE_FILA_DIREITA_PARA_ESQUERDA__ || \
          __QUEUE_TIPO_DE_BUSCA_NA_FILA__ == __QUEUE_PERCORRE_FILA_ESQUERDA_PARA_DIREITA__
    queue_t *elem_remove;
    #endif

    if (queue == NULL)
    {
        __QUEUE_STD_MSG_ERRO__("Endereco do ponteiro da fila eh null");
        return false;
    }

    if (elem == NULL)
    {
        __QUEUE_STD_MSG_ERRO__("Endereco do elemento eh null");
        return false;
    }

    if (elem->next == NULL || elem->prev == NULL)
    {
        __QUEUE_STD_MSG_ERRO__("Elemento nao esta em nenhuma fila");
        return false;
    }

    // Verifica se o elemento a ser removido eh o primeiro da fila
    if (elem == (*queue))
    {
        // Verifica se ha mais de um elemento na fila
        if (elem->next != elem)
        {
            // Troca o endereco do primeiro elemento da fila
            (*queue) = elem->next;
        }
        else
        {
            // Se chegou aqui entao ha apenas um elemento na fila
            // troca o endereco do primeiro elemento da fila para NULL
            (*queue) = NULL;
        }

        return true;
    }

    #if __QUEUE_TIPO_DE_BUSCA_NA_FILA__ == __QUEUE_PERCORRE_FILA_EM_AMBOS_OS_SENTIDOS__
    // Verre a fila de ambos os lados - da direita para esquerda e vice-versa- ate encontrar
    // o elemento a ser retirado ou varrer toda a fila e nao o encontrar
    for (elem_remove_to_left = (*queue)->prev, elem_remove_to_right = (*queue)->next; 
         elem_remove_to_left != elem && elem_remove_to_right != elem; 
         elem_remove_to_left = elem_remove_to_left->prev, elem_remove_to_right = elem_remove_to_right->next)
    #elif __QUEUE_TIPO_DE_BUSCA_NA_FILA__ == __QUEUE_PERCORRE_FILA_DIREITA_PARA_ESQUERDA__
    for (elem_remove = (*queue)->prev; elem_remove != elem; elem_remove = elem_remove->prev)
    #elif __QUEUE_TIPO_DE_BUSCA_NA_FILA__ == __QUEUE_PERCORRE_FILA_ESQUERDA_PARA_DIREITA__
    for (elem_remove = (*queue)->next; elem_remove != elem; elem_remove = elem_remove->next)
    #endif
    {
        // Verifica se percorreu toda a fila e o elemento nao foi encontrado.
        #if   __QUEUE_TIPO_DE_BUSCA_NA_FILA__ == __QUEUE_PERCORRE_FILA_EM_AMBOS_OS_SENTIDOS__
        if (elem_remove_to_left->prev == (*queue) || elem_remove_to_right->next == (*queue))
        #elif __QUEUE_TIPO_DE_BUSCA_NA_FILA__ == __QUEUE_PERCORRE_FILA_DIREITA_PARA_ESQUERDA__
        if (elem_remove->prev == (*queue))
        #elif __QUEUE_TIPO_DE_BUSCA_NA_FILA__ == __QUEUE_PERCORRE_FILA_ESQUERDA_PARA_DIREITA__
        if (elem_remove->next == (*queue))
        #endif
        {
            __QUEUE_STD_MSG_ERRO__("Elemento para remocao nao encontrado na fila");
            return false;
        }
    }

    return true;
}