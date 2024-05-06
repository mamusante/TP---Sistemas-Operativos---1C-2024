#ifndef SERVIDOR_H_
#define SERVIDOR_H_

#include "m_gestor.h"

// *************** DECLARACIÓN DE FUNCIONES **********
// ********* INICIO MEMORIA COMO SERVIDOR *********
void inicializar_servidor();

// ********* ESPERO QUE MODULOS SE CONECTEN COMO CLIENTES *********
void server_para_cpu();
void server_para_kernel();
void server_para_io();

#endif