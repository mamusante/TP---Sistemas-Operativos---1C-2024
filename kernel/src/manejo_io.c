#include "manejo_io.h"


//Dejo mi idea de funcionamiento de manejo de io:
//Cada vez que se conecta un ainterfaz se arma un hilo q se queda escuchando a la interfaz, para mandarle cosas a cada interfaz lo haremos desde el lugar q corresponda segun las verificaciones q haya q hacer (si existe la interfaz, si acepta cierta instruccion, etc). Cada interfaz tendra una cola de procesos esperando para usarla.
//A cada interfaz accedemos desde la queue que arme q es global que se llama cola_interfaces_conectadas buscando la q corresponda por el nombre (el nombre es un ENUM)
//Al desconectarse una interfaz tenemos q sacarla de la cola de interfaces conectadas, borrar su cola de procesos en espera y poner los procesos en estado ready o lo que corresopnda nuevamente y luego hacer el free de la estructura.

void crear_interfaz(op_code interfaz_nueva, int socket, char* nombre_interfaz)
{
    //inicializo una nueva interfaz
    log_info(log_kernel, "se conecto una interfaz nueva, hola! %s \n", nombre_interfaz);
	interfaz_kernel* nueva_interfaz = malloc(sizeof(interfaz_kernel)); 
	nueva_interfaz->tipo_interfaz= interfaz_nueva;
	nueva_interfaz->cola_de_espera = queue_create();
	nueva_interfaz->socket = socket;
    nueva_interfaz->nombre_interfaz = nombre_interfaz;
    sem_init(&nueva_interfaz->sem_puedo_mandar_operacion, 0,1);
    sem_init(&nueva_interfaz->sem_hay_procesos_esperando,0,0);
    //llega hasta aca, hace sgm fault

    log_info(log_kernel, "estoy por intentar inicializar el mutex");
    pthread_mutex_init(&(nueva_interfaz->mutex_cola), NULL);
    log_info(log_kernel, "ya inicialice el mutex");

    //agrego la interfaz a la cola de las q estan conectadas
    pthread_mutex_lock(&mutex_cola_de_interfaces);
	queue_push(cola_interfaces_conectadas, nueva_interfaz); 
    pthread_mutex_unlock(&mutex_cola_de_interfaces);
    log_info(log_kernel, "ya agregue la interfaz a la cola");

	pthread_t hilo_de_escucha_interfaz, hilo_de_envio_a_interfaz; // Creo un hilo
    thread_args_escucha_io args_hilo = {nueva_interfaz}; // En sus args le cargo el la interfaz


    //este hilo se quedara escuchando lo que le devuelva la interfaz y hara lo q corresponda con cada proceso q la llamo al recibirlo de nuevo, tendriamos q chequear si la interfaz devuelve un proceso u otra cosa, en cuyo caso tendriamos q sacar el proceso de otro lado para volverlo a poner en ready o lo q sea
    pthread_create(&hilo_de_escucha_interfaz, NULL, (void*)escucha_interfaz,(void*)&args_hilo);
    pthread_create(&hilo_de_envio_a_interfaz, NULL, (void*)envio_interfaz,(void*)&args_hilo);

    
    pthread_join(hilo_de_escucha_interfaz, NULL); 
    pthread_cancel(hilo_de_envio_a_interfaz); 


    return;
	
}

void envio_interfaz(thread_args_escucha_io* args)
{
    interfaz_kernel* interfaz = args->interfaz;
    argumentos_para_io* args_mandar_a_io;

    log_info(log_kernel, "Estoy en envio interfaz");
    while(1)
    {
        sem_wait(&interfaz->sem_hay_procesos_esperando);
        sem_wait(&interfaz->sem_puedo_mandar_operacion);
        pthread_mutex_lock(&interfaz->mutex_cola);
        args_mandar_a_io = queue_pop(interfaz->cola_de_espera);
        pthread_mutex_unlock(&interfaz->mutex_cola);
        interfaz->proceso_en_interfaz = args_mandar_a_io->proceso;
        enviar_instruccion_io(interfaz->socket,args_mandar_a_io);
    }
}

void escucha_interfaz(thread_args_escucha_io* args)
{
    bool interfaz_conectada = true;
    op_code cod_op;
    interfaz_kernel* interfaz = args->interfaz;
    t_buffer* buffer;

    log_info(log_kernel, "estoy en escucha interfaz");

    while(interfaz_conectada)
    {
        //se queda esperando que la interfaz me mande algo
        cod_op = recibir_operacion(interfaz->socket); //me habla la interfaz
        
        log_info(log_kernel, "El codop antes del switch (manejo_io) es: %d", cod_op);
        
        switch (cod_op) { 
            //aca habria que agregar los posibles codigos de operacion q pueden llegar, dsp me fijo bien            //ya se termino la operacion de entrada salida, devuelvo el proceso a ready
            case MENSAJE:
                log_warning(log_kernel, "Me llegó un mensaje (yo quería FIN_OP_IO)");  
                //recibir_mensaje(interfaz->socket, log_kernel);
                break;
            case FIN_OP_IO:

                if(interfaz->proceso_en_interfaz->pid == pid_eliminar)
                {
                    accionar_segun_estado(interfaz->proceso_en_interfaz,1, INTERRUPTED_BY_USER) ; //lo mando a exit

                }else {
                    accionar_segun_estado(interfaz->proceso_en_interfaz,0, -1);
                } // lo mando a ready 

                //flag 0 para q lo mande a la cola de ready
                sem_post(&interfaz->sem_puedo_mandar_operacion);
                
                buffer = recibiendo_paquete_personalizado(interfaz->socket);
                free(buffer);
                
                break;
            case -1://se desconecta la interfaz
                interfaz_conectada = false;
                desconectar_interfaz(interfaz);
				break;
			default:
				log_warning(log_kernel,"Estamos en manejo io escucha interfaz: Operacion desconocida. No quieras meter la pata");

			    break;
	    };
	}
    return;
}


void desconectar_interfaz(interfaz_kernel* interfaz)
{
    interfaz_kernel* interfaz_aux;
    pthread_mutex_lock(&mutex_cola_de_interfaces);
    interfaz_aux = queue_pop(cola_interfaces_conectadas);
    while(interfaz_aux->nombre_interfaz != interfaz->nombre_interfaz)
    {
        queue_push(cola_interfaces_conectadas, interfaz_aux);
        interfaz_aux = queue_pop(cola_interfaces_conectadas);
    }
    pthread_mutex_unlock(&mutex_cola_de_interfaces);

    //tengo q mandar estos a procesos a ready o exit???
    queue_destroy(interfaz_aux->cola_de_espera);// faltaria hacer algo con los procesos de la cola de espera

    sem_destroy(&interfaz_aux->sem_puedo_mandar_operacion);
    sem_destroy(&interfaz_aux->sem_hay_procesos_esperando);
    pthread_mutex_destroy(&interfaz_aux->mutex_cola);

    free(interfaz_aux);
    return;
}

void enviar_instruccion_io(int socket, argumentos_para_io* args)
{
    t_paquete* paquete_instruccion = crear_paquete_personalizado(args->operacion); // Creo un paquete personalizado con un codop para que IO reconozca lo que le estoy mandando
    
    agregar_int_al_paquete_personalizado(paquete_instruccion, args->proceso->pid); //le mando el pid del proceso
    
    log_info(log_kernel, "el codop fuera del switch es %d", args->operacion);
    log_info(log_kernel, "el numero que tiene IO_GEN_SLEEP es: %d", IO_GEN_SLEEP);
    
    switch (args->operacion)
    {
        case IO_GEN_SLEEP:
            log_info(log_kernel,"entre al agregar int al paquete desde io_gen_sleep");
            agregar_int_al_paquete_personalizado(paquete_instruccion, args->unidades_de_trabajo);
            break;
        case IO_STDIN_READ:
        case IO_STDOUT_WRITE:
            log_info(log_kernel,"entre al agregar al paquete desde STDIN Y OUT");
            agregar_lista_al_paquete_personalizado(paquete_instruccion, args->registro_direccion, sizeof(t_direccion_fisica));
            agregar_int_al_paquete_personalizado(paquete_instruccion, args->registro_tamano);   
            break;
        case IO_FS_CREATE:
        case IO_FS_DELETE:
            agregar_string_al_paquete_personalizado(paquete_instruccion, args->nombre_archivo);
            break;
        case IO_FS_TRUNCATE:
            agregar_string_al_paquete_personalizado(paquete_instruccion, args->nombre_archivo);
            agregar_int_al_paquete_personalizado(paquete_instruccion, args->registro_tamano);
            break;
        case IO_FS_WRITE:
        case IO_FS_READ:
            agregar_string_al_paquete_personalizado(paquete_instruccion, args->nombre_archivo);
            agregar_estructura_al_paquete_personalizado(paquete_instruccion, args->registro_direccion, sizeof(t_list));
            agregar_int_al_paquete_personalizado(paquete_instruccion, args->registro_tamano);
            agregar_int_al_paquete_personalizado(paquete_instruccion, args->registro_puntero_archivo);
            break;
        default:
            break;
    }
   
    free(args);

    enviar_paquete(paquete_instruccion, socket); // Envio el paquete a través del socket

    eliminar_paquete(paquete_instruccion); // Libero el paquete

    return;
}

interfaz_kernel* verificar_interfaz(char* nombre_interfaz_buscada, op_code tipo_interfaz_buscada)
{
    //Esta funcion verifica q exista la interfaz con el nombre pasado por parametro y que su tipo se corresponda con el pasado por parametro
    interfaz_kernel* aux;

    if(queue_is_empty(cola_interfaces_conectadas) == false){ // Si la cola no está vacía

        log_info(log_kernel, "entre al if de q hay interfaces en la cola");
        pthread_mutex_lock(&mutex_cola_de_interfaces);
        aux  = queue_pop(cola_interfaces_conectadas);
        char* primer_nombre = aux->nombre_interfaz;

        while(strcmp(aux->nombre_interfaz,nombre_interfaz_buscada) !=0) 
        {

            queue_push(cola_interfaces_conectadas, aux);
            aux = queue_pop(cola_interfaces_conectadas);
            if(strcmp(aux->nombre_interfaz, primer_nombre) == 0)
            {
                queue_push(cola_interfaces_conectadas, aux);
                aux->nombre_interfaz = "nada";
                return aux; //No esta la interfaz con ese nombre, retorno null
            }
        }
        queue_push(cola_interfaces_conectadas, aux);
        pthread_mutex_unlock(&mutex_cola_de_interfaces);
        //Si llegamos a este punto es porque se encontro la interfaz
        if(aux->tipo_interfaz == tipo_interfaz_buscada)
        {
            log_info(log_kernel, "entre al if de q el tipo de interfaz es el q qiero");
            return aux; //La interfaz es del tipo q quiero, la devuelvo

        }else{
            aux->nombre_interfaz = "nada";
            return aux; //No esta la interfaz con ese nombre, retorno null//No coincide el tipo de interfaz, mando null
        }
    }
    
    return aux;
}
//********************************FUNCIONES DE INSTRUCCIONES DE IO**********************

int io_gen_sleep(char* nombre_interfaz, int unidades_de_trabajo, pcb* proceso)
{

    log_info(log_kernel, "las unidades de trabajo son: %d", unidades_de_trabajo);
    log_info(log_kernel, "el nombre de la interfaz es: %s", nombre_interfaz);
    //ante una petición van a esperar una cantidad de unidades de trabajo, cuyo valor va a venir dado en la petición desde el Kernel.
    interfaz_kernel* interfaz = verificar_interfaz(nombre_interfaz, GENERICA);

    log_info(log_kernel, "Nombre de la interfaz despues de verificar: %s", interfaz->nombre_interfaz);
    
    argumentos_para_io* args = malloc(sizeof(argumentos_para_io));
    args->unidades_de_trabajo = unidades_de_trabajo;
    args->proceso = proceso;
    args->operacion = IO_GEN_SLEEP;

    if(strcmp(interfaz->nombre_interfaz, nombre_interfaz) == 0) // si no devuelve null es porq encontro la interfaz q queria y es del tipo q queria
    {
        //aca hago lo q tengo q hacer, es decir bloquear el proceso y madnarle la isntruccion a la interfaz
        pthread_mutex_lock(&interfaz->mutex_cola);
        queue_push(interfaz->cola_de_espera, args);
        pthread_mutex_unlock(&interfaz->mutex_cola);
        sem_post(&interfaz->sem_hay_procesos_esperando);

        log_info(log_kernel, "PID: <%d> - Bloqueado por: <%s>", proceso->pid, nombre_interfaz);
        pasar_proceso_a_blocked(proceso);

        return -1; //que no haga nada porq ya lo bloquie yo

    }else{
        log_info(log_kernel, "Si pasas por aca sos muy puto kernel");
        pasar_proceso_a_exit(proceso, INVALID_INTERFACE);
        return -1;//no hago nada porq ya lo finalizo yo
    }

}

int io_stdin_read(char* nombre_interfaz, t_list* registro_direccion, uint32_t registro_tamano, pcb* proceso)
{
    // Esta instrucción solicita al Kernel que mediante la interfaz ingresada se lea desde el STDIN (Teclado) un valor cuyo tamaño está delimitado por el valor del Registro Tamaño y el mismo se guarde a partir de la Dirección Lógica almacenada en el Registro Dirección.
    log_info(log_kernel, "el registro tamano es: %u", registro_tamano);
    log_info(log_kernel, "el nombre de la interfaz es: %d", nombre_interfaz);
    interfaz_kernel* interfaz = verificar_interfaz(nombre_interfaz, STDIN);
    argumentos_para_io* args = malloc(sizeof(argumentos_para_io));
    args->registro_direccion = registro_direccion;
    args->registro_tamano = registro_tamano;
    args->proceso = proceso;
    args->operacion = IO_STDIN_READ;

    if(interfaz) // si no devuelve null es porq encontro la interfaz q queria y es del tipo q queria
    {
        pthread_mutex_lock(&interfaz->mutex_cola);
        queue_push(interfaz->cola_de_espera, args);
        pthread_mutex_unlock(&interfaz->mutex_cola);
        log_info(log_kernel, "PID: <%d> - Bloqueado por: <%s>", proceso->pid, nombre_interfaz);
        pasar_proceso_a_blocked(proceso);
        sem_post(&interfaz->sem_hay_procesos_esperando);
        return -1; //que no haga nada porq ya lo bloquie yo

    }else{
        pasar_proceso_a_exit(proceso, INVALID_INTERFACE);
        return -1;//no hago nada porq ya lo finalizo yo
    }
}

int io_stdout_write(char* nombre_interfaz, t_list* registro_direccion, uint32_t registro_tamano, pcb* proceso)
{
    interfaz_kernel* interfaz = verificar_interfaz(nombre_interfaz, STDOUT);
    argumentos_para_io* args = malloc(sizeof(argumentos_para_io));
    args->registro_direccion = registro_direccion;
    args->registro_tamano = registro_tamano;
    args->proceso = proceso;
    args->operacion = IO_STDOUT_WRITE;

    if(interfaz) // si no devuelve null es porq encontro la interfaz q queria y es del tipo q queria
    {
        
        pthread_mutex_lock(&interfaz->mutex_cola);
        queue_push(interfaz->cola_de_espera, args);
        pthread_mutex_unlock(&interfaz->mutex_cola);
        log_info(log_kernel, "PID: <%d> - Bloqueado por: <%s>", proceso->pid, nombre_interfaz);
        pasar_proceso_a_blocked(proceso);
        sem_post(&interfaz->sem_hay_procesos_esperando);
        return -1; //que no haga nada porq ya lo bloquie yo

    }else{
        pasar_proceso_a_exit(proceso, INVALID_INTERFACE);
        return -1;//no hago nada porq ya lo finalizo yo
    }
}

int io_fs_create(char* nombre_interfaz, char* nombre_archivo, pcb* proceso)
{
    interfaz_kernel* interfaz = verificar_interfaz(nombre_interfaz, DIALFS);
    argumentos_para_io* args = malloc(sizeof(argumentos_para_io));
    args->nombre_archivo = nombre_archivo;
    args->operacion = IO_FS_CREATE;

    if(interfaz) // si no devuelve null es porq encontro la interfaz q queria y es del tipo q queria
    {
        pthread_mutex_lock(&interfaz->mutex_cola);
        queue_push(interfaz->cola_de_espera, args);
        pthread_mutex_unlock(&interfaz->mutex_cola);
        log_info(log_kernel, "PID: <%d> - Bloqueado por: <%s>", proceso->pid, nombre_interfaz);
        pasar_proceso_a_blocked(proceso);
        sem_post(&interfaz->sem_hay_procesos_esperando);
        return -1; //que no haga nada porq ya lo bloquie yo

    }else{
        pasar_proceso_a_exit(proceso, INVALID_INTERFACE);
        return -1;//no hago nada porq ya lo finalizo yo
    }
}

int io_fs_delete(char* nombre_interfaz, char* nombre_archivo, pcb* proceso)
{
    interfaz_kernel* interfaz = verificar_interfaz(nombre_interfaz, DIALFS);
    argumentos_para_io* args = malloc(sizeof(argumentos_para_io));
    args->nombre_archivo = nombre_archivo;
    args->operacion = IO_FS_DELETE;
    
    if(interfaz) // si no devuelve null es porq encontro la interfaz q queria y es del tipo q queria
    {
        pthread_mutex_lock(&interfaz->mutex_cola);
        queue_push(interfaz->cola_de_espera, args);
        pthread_mutex_unlock(&interfaz->mutex_cola);
        log_info(log_kernel, "PID: <%d> - Bloqueado por: <%s>", proceso->pid, nombre_interfaz);
        pasar_proceso_a_blocked(proceso);
        sem_post(&interfaz->sem_hay_procesos_esperando);
        return -1; //que no haga nada porq ya lo bloquie yo

    }else{
        pasar_proceso_a_exit(proceso, INVALID_INTERFACE);
        return -1;//no hago nada porq ya lo finalizo yo
    }
}

int io_fs_truncate(char* nombre_interfaz, char* nombre_archivo, int registro_tamano, pcb* proceso)
{
    interfaz_kernel* interfaz = verificar_interfaz(nombre_interfaz, DIALFS);
    argumentos_para_io* args = malloc(sizeof(argumentos_para_io));
    args->nombre_archivo = nombre_archivo;
    args->registro_tamano = registro_tamano;
    args->operacion = IO_FS_TRUNCATE;
    
    if(interfaz) // si no devuelve null es porq encontro la interfaz q queria y es del tipo q queria
    {
        pthread_mutex_lock(&interfaz->mutex_cola);
        queue_push(interfaz->cola_de_espera, args);
        pthread_mutex_unlock(&interfaz->mutex_cola);
        log_info(log_kernel, "PID: <%d> - Bloqueado por: <%s>", proceso->pid, nombre_interfaz);
        pasar_proceso_a_blocked(proceso);
        sem_post(&interfaz->sem_hay_procesos_esperando);
        return -1; //que no haga nada porq ya lo bloquie yo

    }else{
        pasar_proceso_a_exit(proceso, INVALID_INTERFACE);
        return -1;//no hago nada porq ya lo finalizo yo
    }
}

int io_fs_write(char* nombre_interfaz, char* nombre_archivo, t_list*  registro_direccion, int registro_tamano, int registro_puntero_archivo, pcb* proceso)
{
    interfaz_kernel* interfaz = verificar_interfaz(nombre_interfaz, DIALFS);
    argumentos_para_io* args = malloc(sizeof(argumentos_para_io));
    args->nombre_archivo = nombre_archivo;
    args->registro_tamano = registro_tamano;
    args->registro_direccion = registro_direccion;
    args->registro_puntero_archivo = registro_puntero_archivo;
    args->operacion = IO_FS_WRITE;
    
    if(interfaz) // si no devuelve null es porq encontro la interfaz q queria y es del tipo q queria
    {
        pthread_mutex_lock(&interfaz->mutex_cola);
        queue_push(interfaz->cola_de_espera, args);
        pthread_mutex_unlock(&interfaz->mutex_cola);
        log_info(log_kernel, "PID: <%d> - Bloqueado por: <%s>", proceso->pid, nombre_interfaz);
        pasar_proceso_a_blocked(proceso);
        sem_post(&interfaz->sem_hay_procesos_esperando);
        return -1; //que no haga nada porq ya lo bloquie yo

    }else{
        pasar_proceso_a_exit(proceso, INVALID_INTERFACE);
        return -1;//no hago nada porq ya lo finalizo yo
    }
}

int io_fs_read(char* nombre_interfaz, char* nombre_archivo, t_list* registro_direccion, int registro_tamano, int registro_puntero_archivo, pcb* proceso)
{
    interfaz_kernel* interfaz = verificar_interfaz(nombre_interfaz, DIALFS);
    argumentos_para_io* args = malloc(sizeof(argumentos_para_io));
    args->nombre_archivo = nombre_archivo;
    args->registro_tamano = registro_tamano;
    args->registro_direccion = registro_direccion;
    args->registro_puntero_archivo = registro_puntero_archivo;
    args->operacion = IO_FS_READ;
    
    if(interfaz) // si no devuelve null es porq encontro la interfaz q queria y es del tipo q queria
    {
        pthread_mutex_lock(&interfaz->mutex_cola);
        queue_push(interfaz->cola_de_espera, args);
        pthread_mutex_unlock(&interfaz->mutex_cola);
        log_info(log_kernel, "PID: <%d> - Bloqueado por: <%s>", proceso->pid, nombre_interfaz);
        pasar_proceso_a_blocked(proceso);
        sem_post(&interfaz->sem_hay_procesos_esperando);
        return -1; //que no haga nada porq ya lo bloquie yo

    }else {
        pasar_proceso_a_exit(proceso, INVALID_INTERFACE);
        return 1;//mando el proceso a exit
    }

}



