#include "atenderKERNEL.h"

//******************************************************************
//******************* CREACIÓN DE PROCESO **************************
//******************************************************************
void iniciar_estructura_para_un_proceso_nuevo(t_buffer* buffer){
	char* path = recibir_string_del_buffer(buffer);
	int pid = recibir_int_del_buffer(buffer);
            
    log_info(log_memoria, "Proceso %d recibido, direccion del path: %s", pid, path);

	// Crear un proceso
	t_proceso* proceso_nuevo = crear_proceso(pid, path);

	//Agregar a la lista de procesos 
	list_add(lista_procesos_recibidos, proceso_nuevo);
    log_info(log_memoria, "Agregue el proceso a lista");

	log_info(log_memoria, "PROCESO CREADO CON ÉXITO");

	// Creo tabla de páginas
	// Podriamos mandar un mensaje para chequear que llegó
}

t_proceso* crear_proceso(int pid, char* path_instruc){
	t_proceso* proceso_nuevo = malloc(sizeof(t_proceso));

	proceso_nuevo->pid = pid;
	proceso_nuevo->path = path_instruc;
	proceso_nuevo->instrucciones = NULL;
    proceso_nuevo->tabla_paginas = list_create();

	// Cargo instrucciones
	proceso_nuevo->instrucciones = leer_archivo_y_cargar_instrucciones(proceso_nuevo->path);

	return proceso_nuevo;
}

//********** LEE EL ARCHIVO Y CARGA LAS INSTRUCCIONES EN UNA LISTA (LAS INSTRUCCIONES LAS DEJA DEL TIPO "t_instruccion_codigo")
t_list* leer_archivo_y_cargar_instrucciones(char* archivo_pseudocodigo) {
    //Abro el archivo en modo lectura

    // EN CONFIG ME DICE DONDE ESTAN LOS PATH
    //char* direccion = strcat(config_memoria->path_instrucciones, archivo_pseudocodigo);
    // ver si en config tengo que agregar una /
    // POR AHORA DEJO LA LINEA DE ABAJO
    // para no hacer que todos tengan que crear una carpeta "sripts-pruebas"

    FILE* archivo = fopen(archivo_pseudocodigo, "r");

    t_list* instrucciones = list_create(); //Creo una lista para almacenar todas las instrucciones
    
    char* instruccion_formateada = NULL;
    int i = 0;
    if (archivo == NULL) {
        log_error(log_memoria, "No se puede abrir el archivo_pseudocodigo para leer instrucciones.");  
        return instrucciones;
    }

    char* linea_instruccion = malloc(256 * sizeof(int));
    while (fgets(linea_instruccion, 256, archivo)) {
    	int size_linea_actual = strlen(linea_instruccion);
    	if(size_linea_actual > 2){
    		if(linea_instruccion[size_linea_actual - 1] == '\n'){
				char* linea_limpia = string_new();
				string_n_append(&linea_limpia, linea_instruccion, size_linea_actual - 1);
				free(linea_instruccion);
				linea_instruccion = malloc(256 * sizeof(int));
				strcpy(linea_instruccion,linea_limpia);
    		}
    	}
    	//-----------------------------------------------
        char** l_instrucciones = string_split(linea_instruccion, " ");
        log_info(log_memoria, "Instruccion: [%s]", linea_instruccion);
        while (l_instrucciones[i]) {
        	i++;
        }

        t_instruccion_codigo* pseudo_cod = malloc(sizeof(t_instruccion_codigo));
        pseudo_cod->mnemonico = strdup(l_instrucciones[0]);
        //log_info(log_memoria, "%s", pseudo_cod->mnemonico);

        pseudo_cod->primero_parametro = (i > 1) ? strdup(l_instrucciones[1]) : NULL;
        //if(i > 1) log_info(log_memoria, "%s", pseudo_cod->primero_parametro);
        pseudo_cod->segundo_parametro = (i > 2) ? strdup(l_instrucciones[2]) : NULL;
        //if(i > 2) log_info(log_memoria, "%s", pseudo_cod->segundo_parametro);
        pseudo_cod->tercero_parametro = (i > 3) ? strdup(l_instrucciones[3]) : NULL;
        //if(i > 3) log_info(log_memoria, "%s", pseudo_cod->tercero_parametro);
        pseudo_cod->cuarto_parametro = (i > 4) ? strdup(l_instrucciones[4]) : NULL;
        //if(i > 4) log_info(log_memoria, "%s", pseudo_cod->cuarto_parametro);
        pseudo_cod->quinto_parametro = (i > 5) ? strdup(l_instrucciones[5]) : NULL;
        //if(i > 5) log_info(log_memoria, "%s", pseudo_cod->quinto_parametro);


        if (i == 5) {
            instruccion_formateada = string_from_format("%s %s %s %s %s", pseudo_cod->mnemonico, pseudo_cod->primero_parametro, pseudo_cod->segundo_parametro, pseudo_cod->tercero_parametro, pseudo_cod->cuarto_parametro);
        } else if(i == 4){
            instruccion_formateada = string_from_format("%s %s %s %s", pseudo_cod->mnemonico, pseudo_cod->primero_parametro, pseudo_cod->segundo_parametro, pseudo_cod->tercero_parametro);
        } else if(i == 3){
            instruccion_formateada = string_from_format("%s %s %s", pseudo_cod->mnemonico, pseudo_cod->primero_parametro, pseudo_cod->segundo_parametro);
        } else if (i == 2) {
            instruccion_formateada = string_from_format("%s %s", pseudo_cod->mnemonico, pseudo_cod->primero_parametro);
        } else {
            instruccion_formateada = strdup(pseudo_cod->mnemonico);
        }

        list_add(instrucciones, instruccion_formateada);
        
        for (int j = 0; j < i; j++) {
            free(l_instrucciones[j]);
        }

        free(l_instrucciones);
        free(pseudo_cod->mnemonico);
		if(pseudo_cod->primero_parametro) free(pseudo_cod->primero_parametro);
		if(pseudo_cod->segundo_parametro) free(pseudo_cod->segundo_parametro);
        if(pseudo_cod->tercero_parametro) free(pseudo_cod->tercero_parametro);
        if(pseudo_cod->cuarto_parametro) free(pseudo_cod->cuarto_parametro);
        if(pseudo_cod->quinto_parametro) free(pseudo_cod->quinto_parametro);

		free(pseudo_cod);
        i = 0; // Restablece la cuenta para la próxima iteración
    }

    fclose(archivo);
    free(linea_instruccion);
    return instrucciones;
}

//******************************************************************
//******************** FINALIZAR PROCESO ***************************
//******************************************************************
void liberar_memoria_proceso(t_buffer* buffer){
    // RECIBO UN BUFFER [PID] -> Int
    // DEBO LIBERAR ESPACIO, Y SACAR EL PROCESO DE LA LISTA

    int pid_a_eliminar = recibir_int_del_buffer(buffer);
    t_proceso* proceso_a_eliminar = obtener_proceso_por_id(pid_a_eliminar);
    
    if(list_remove_element(lista_procesos_recibidos, proceso_a_eliminar)){
		// ELIMINO EL PROCESO DE LA LISTA 
		log_info(log_memoria, "PROCESO <PID:%d> ELIMINADO DE MEMORIA", pid_a_eliminar);
	} else {
		log_info(log_memoria, "Proceso no encontrado en la lista de procesos para ser eliminados");
		//exit(EXIT_FAILURE);
	}

    // LIBERO ESPACIO, MARCO FRAMES COMO LIBRES
    // recorro posiciones de la tabla de página, me fijo bit de P -> si es ==1 marco frame LIBRE
    t_list* tabla_paginas_a_eliminar = proceso_a_eliminar->tabla_paginas;

    for (int i = 0; i < list_size(tabla_paginas_a_eliminar); i++) {
        t_fila_tabla_paginas* pagina_a_eliminar = list_get(tabla_paginas_a_eliminar, i);

        if (pagina_a_eliminar->presencia == 1) {
            pthread_mutex_lock (&mutex_bitmap_marcos);
            bitarray_clean_bit(bitmap_marcos, pagina_a_eliminar->frame);
            pthread_mutex_unlock (&mutex_bitmap_marcos);
        }
    }
    
}