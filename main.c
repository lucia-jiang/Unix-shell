#include <stddef.h> /* NULL */
#include <stdio.h>	/* setbuf, printf */
#include <stdlib.h>

#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <ctype.h>
#include <wordexp.h>
#include <pwd.h>
#include <uuid/uuid.h>
#include <glob.h>

extern int obtain_order(); /* See parser.y for description */

void cambiarEntrada(int fd);
void cambiarSalida(int fd);
void cambiarSalidaError(int fd);
int mandatoSimple(void);
int secuencia(int argvc);
void esperarPadre(int pid);
int mandatoCd(int argc, int i);
int mandatoUmask(int argc, int i);
int obtenerMascaraActual();
int mandatoLimit(int argc, int i);
int obtenerIntMandatoLimit(int i);
int mandatosInternos(int i, int *status);
int mandatoSet(int argc, int i);
int metacaracteres(int argvc, int argc);
int expansion(int argvc, int argc);
void misetenv(char *var, int num);

char ***argvv = NULL;
int bg;

int main(void) {
	
	int argc;
	int argvc;
	char **argv = NULL;
	char *filev[3] = {NULL, NULL, NULL}; //adónde redirigir ficheros
	
	int ret;

	//Almacenar la entrada estándar, salida y la de error
	int fd_entrada = dup(0); //entrada estándar
	int fd_salida = dup(1); //salida estándar
	int fd_salida_error = dup(2); //salida de error

	//señales
	struct sigaction act;

	//por defecto, ignorar señales de SIGINT y SIGQUIT
	act.sa_handler = SIG_IGN;
	sigaction(SIGINT, &act, NULL);
	sigaction(SIGQUIT, &act, NULL);

	setbuf(stdout, NULL); /* Unbuffered */
	setbuf(stdin, NULL);

	// Variables especiales
	setenv("prompt", "msh >", 1);
	misetenv("mypid", getpid());
	setenv("bgpid", "0", 1);
	setenv("status", "0", 1);

	while (1) {
		//al empezar bucle poner de nuevo entradas que son:
		dup2(fd_entrada, 0);
		dup2(fd_salida, 1);
		dup2(fd_salida_error, 2);

		fprintf(stderr, "%s", "msh> "); /* Prompt */
		
		ret = obtain_order(&argvv, filev, &bg);
		if (ret == 0)
			break; /* EOF */
		if (ret == -1)
			continue;	 /* Syntax error */
		argvc = ret - 1; /* Line */
		if (argvc == 0)
			continue; /* Empty line */
#if 1

		//argvc es el número de argumentos unidos por pipes
		//argc tendrá el número de "palabras" del último mandato
		//evaluamos si se puede hacer la expansión de caracteres o metacaracteres

		for (argvc = 0; (argv = argvv[argvc]); argvc++) {
			for (argc = 0; argv[argc]; argc++) {
				if ((argv[argc][0] == '~') || (strchr(argv[argc], '$'))) { //empieza por tilde o contiene el $
					metacaracteres(argvc, argc);		
				} 
				if (strchr(argv[argc], '?')) {  //contiene el caracter ?
					expansion(argvc, argc);
				}
			}
		} 

		//---------------------redirecciones-----------------------
		
		int fd;
		if (filev[0]) { /* IN */
			if ((fd = open(filev[0], O_RDONLY)) < 0) {
				perror("Error en el open");
				continue;
			}
			cambiarEntrada(fd);
		}
		if (filev[1]) { /* OUT */
			fd = open(filev[1], O_WRONLY|O_CREAT|O_TRUNC, 0666);
			cambiarSalida(fd);
		}
		if (filev[2]) { /* ERR */
			fd = open(filev[2], O_WRONLY|O_CREAT|O_TRUNC, 0666);
			cambiarSalidaError(fd);
		}
		
		//----------------------clasificar mandatos---------------------	

		if(argvc == 1){ //mandatos simples
			int status;
			if (!mandatosInternos(0, &status)) { //no es mandato interno
				mandatoSimple();
			}
			else {
				misetenv("status", status);
			}
		}
		
		else if (argvc>1) { //pipes
			secuencia(argvc); //le pasamos el número de mandatos
		}		
#endif
	}
	close(fd_entrada);
	close(fd_salida);
	close(fd_salida_error);

	return 0;
}

//cambiamos la entrada estándar al descriptor de fichero que se pasa como parámetro
void cambiarEntrada(int fd) { 
	close(0);
	dup(fd);
	close(fd);
}

//cambiamos la salida estándar al descriptor de fichero que se pasa como parámetro
void cambiarSalida(int fd){ 
	close(1);
	dup(fd);
	close(fd);
}

//cambiamos la salida estándar de error al descriptor de fichero que se pasa como parámetro
void cambiarSalidaError(int fd){ 
	close(2);
	dup(fd);
	close(fd);
}

int mandatoSimple(void) { //mandato simple
	struct sigaction act;
	int pid = fork(); 
	switch(pid){
		case -1: 
			perror("fork");
			return 1;
		case 0: 
			//cambiar el comportamiento de señales en foreground al de por defecto (matar)
			if (!bg) { 
				act.sa_handler = SIG_DFL;
				sigaction(SIGINT, &act, NULL);
				sigaction(SIGQUIT, &act, NULL);
			} 
			execvp(argvv[0][0], argvv[0]);
			perror("exec");
			exit(1);
		default:
			esperarPadre(pid);
	}
	return 0;
}

int secuencia(int argvc) { //pipes

	int i, pid, status;
	int tub_ant[2], tub_post[2];
	struct sigaction act;

	for (i=0; i<argvc; i++){ //i son los diferentes mandatos
		if (i!=0){ //no es el primero
			if (i > 1) {
				close(tub_ant[0]);
				close(tub_ant[1]);
			}
			tub_ant[0] = tub_post[0];
			tub_ant[1] = tub_post[1];
		}
		if (i != argvc-1){
			pipe(tub_post);
		}

		pid = fork();

		if (pid < 0) {
			perror("fork");
			return 1;
		}
		else if (pid == 0){ //hijo
			if (i!=0){
				dup2(tub_ant[0], 0); //poner como entrada estándar
				close(tub_ant[0]);
				close(tub_ant[1]);
			}
			if (i != argvc-1) {
				dup2(tub_post[1], 1); //poner como salida estándar
				close(tub_post[0]);
				close(tub_post[1]);
			}

			if (!bg){ //si es el último mandato de la secuencia y no está en bg debe de matar
				act.sa_handler = SIG_DFL;
				sigaction(SIGINT, &act, NULL);
				sigaction(SIGQUIT, &act, NULL);
			} 

			if (mandatosInternos(i, &status)){ //es mandato interno
				exit(status);
			}
			else {
				execvp(argvv[i][0],argvv[i]); 
				perror("exec");
				exit(1);
			}
		}
		if (i == argvc-1) {
			close(tub_ant[0]);
			close(tub_ant[1]);
		}
	}
	esperarPadre(pid);
	return 0;
}

//llamada que realiza el padre para esperar al hijo cuyo pid se pasa por parámetro
//si el mandato se ejecutó en bg, se actualiza la variable  
void esperarPadre(int pid) {
	int status;
	if(bg) { //background
		misetenv("bgpid", pid);
		fprintf(stderr, "[%d]\n", pid);	
	}
	else { //no está en background
		waitpid(pid, &status, 0);
		misetenv("status", status);
	}		
}


int mandatoCd(int argc, int i){
	char dir[1024];
	if (argc == 1){ //ningún parámetro, mandamos a HOME
		if(chdir(getenv("HOME"))) {
			perror("cd");
			return 1;
		}	
	}
	else if (argc == 2){ //cambiar al directorio que indica el argumento
		if (chdir(argvv[i][1])) {
			perror("cd");
			return 1;
		}	
	}
	else{ //más de un argumento
		perror("cd");
		return 1;
	}
	printf("%s\n", getcwd(dir, 1024));
	return 0;
}

int mandatoUmask(int argc, int i) {
	
	if (argc == 1) {
		printf("%o\n", obtenerMascaraActual());
		return 0;
	}

	else if (argc == 2) {
		char *ptro;	
		int mascara = strtol(argvv[i][1], &ptro, 8); 
		if (*ptro != '\0' || mascara < 0 || mascara > 0777) {
			perror("umask");
			return 1;
		}
		umask(mascara);
		printf("%o\n", mascara);
	}

	else if (argc > 2) {
		perror("umask");
		return 1;
	}
	return 0;
}

//umask devuelve la mascara anterior, por lo que hay que hacer dos umask
int obtenerMascaraActual() { 
	int res = umask(0);
	umask(res); 
	return res;
}

int mandatoLimit(int argc, int i) {
	struct rlimit rlimit;
	
	if (argc == 1) { //imprimir todos los límites
		getrlimit(RLIMIT_CPU, &rlimit); 
		printf("%s\t%lu\n", "cpu", rlimit.rlim_cur);
		getrlimit(RLIMIT_FSIZE, &rlimit); 
		printf("%s\t%lu\n", "fsize", rlimit.rlim_cur);
		getrlimit(RLIMIT_DATA, &rlimit); 
		printf("%s\t%lu\n", "data", rlimit.rlim_cur);
		getrlimit(RLIMIT_STACK, &rlimit); 
		printf("%s\t%lu\n", "stack", rlimit.rlim_cur);
		getrlimit(RLIMIT_CORE, &rlimit); 
		printf("%s\t%lu\n", "core", rlimit.rlim_cur);
		getrlimit(RLIMIT_NOFILE, &rlimit); 
		printf("%s\t%lu\n", "nofile", rlimit.rlim_cur);
		return 0;
	}
	
	int opcion = obtenerIntMandatoLimit(i);
	if (opcion == 10) {
		perror("limit");
		return 1;
	}
	
	if (argc == 2) { //imprimir el límite que se pasa por parámetro
		getrlimit(opcion, &rlimit);
		printf("%s\t%lu\n", argvv[i][1], rlimit.rlim_cur);
	}

	else if (argc == 3) { //establecer límite
		rlimit.rlim_cur = atoi(argvv[i][2]);
		rlimit.rlim_max = atoi(argvv[i][2]);
		
		setrlimit(opcion, &rlimit);
		printf("%s\t%lu\n", argvv[i][1], rlimit.rlim_cur);
	}

	else{
		perror("limit");
		return 1;
	}
	return 0;
}

int obtenerIntMandatoLimit(int i){
	if(strcmp(argvv[i][1], "cpu") == 0) 
		return RLIMIT_CPU;
	if(strcmp(argvv[i][1], "fsize") == 0) 
		return RLIMIT_FSIZE;
	if(strcmp(argvv[i][1], "data") == 0) 
		return RLIMIT_DATA;
	if(strcmp(argvv[i][1], "stack") == 0) 
		return RLIMIT_STACK;
	if(strcmp(argvv[i][1], "core") == 0) 
		return RLIMIT_CORE;
	if(strcmp(argvv[i][1], "nofile") == 0) 
		return RLIMIT_NOFILE;
	return 10; //error
}

int mandatoSet(int argc, int i){
	extern char **environ;
	char **env = environ;
	if (argc == 1) { //listar todas las variables de entorno
		while(*env) {
			printf("%s\n", *env);
			env++;
		}
		return 0;
	}

	else if (argc == 2) {//el valor actual de la variable
		printf("%s=%s\n", argvv[i][1], getenv(argvv[i][1]));
	} 
	else {
		int j, k;
		char * str = (char *) malloc(128*sizeof(char));

		int pos = 0;
		//juntar strings en un puntero con un espacio entre cada palabra
		for (j=2; j<argc; j++) { 
			for (k = 0; argvv[i][j][k]; k++) {
				str[pos++] = argvv[i][j][k];
			}
			str[pos++] = ' ';
		}
		str[pos-1] = '\0';
		setenv(argvv[i][1], str,1);
		free(str);
	}	
	return 0;
}

int mandatosInternos(int i, int *status){

	int argc, ret;
	for (argc = 0; argvv[i][argc]; argc++) {
		//printf("%d %s ", argvc, argv[argc]);
	}

	if (strcmp(argvv[i][0], "cd") == 0) { 
		ret = mandatoCd(argc, i);
		if (status) {
			*status = ret; 
		}
		return 1;
	}

	else if (strcmp(argvv[i][0], "umask") == 0) { 
		ret = mandatoUmask(argc, i);
		if (status) {
			*status = ret; 
		}
		return 1;
	}

	else if (strcmp(argvv[i][0], "limit") == 0) { 
		ret = mandatoLimit(argc, i);
		if (status) {
			*status = ret; 
		}
		return 1;
	}

	else if (strcmp(argvv[i][0], "set") == 0) { 
		ret = mandatoSet(argc, i);
		if (status) {
			*status = ret; 
		}
		return 1;
	}
	return 0;
}

int metacaracteres(int argvc, int argc) { //suponiendo solo un $ 
	wordexp_t result;
	int status = wordexp (argvv[argvc][argc], &result, 0);
	if (result.we_wordv[0] == NULL) {
		return 0;
	}
	switch (status) {
		case 0:	
			argvv[argvc][argc] = realloc(argvv[argvc][argc], (strlen(result.we_wordv[0]) + 1 )* sizeof(char));
			if (argvv[argvc][argc] == NULL) {
				perror("realloc fallido");
				return 1;
			} 
			strcpy(argvv[argvc][argc], result.we_wordv[0]);
			break;
		case WRDE_NOSPACE:
			wordfree(&result);
		default:  //otro error
			perror("Metacaracter");           
			return 1;
	}
	wordfree(&result);
	return 0;
}

int expansion(int argvc, int argc) {
	wordexp_t result;
	int pos, i, j;
	int longitud = 0;
	int status = wordexp (argvv[argvc][argc], &result, 0);

	if (result.we_wordv[0] == NULL) {
		perror("Expansión");
		return 1;
	}
	switch (status) {
		case 0:	
			for (i = 0; i<result.we_wordc; i++) {
				longitud += strlen(result.we_wordv[i]);
			}
			argvv[argvc][argc] = realloc(argvv[argvc][argc], (longitud + 1 )* sizeof(char));
			if (argvv[argvc][argc] == NULL) {
				perror("Realloc");
				return 1;
			} 
			pos = 0;
			for (i = 0; i<result.we_wordc; i++) { 
				for (j=0; j<strlen(result.we_wordv[i]); j++) {
					argvv[argvc][argc][pos++] = result.we_wordv[i][j];
				}
				argvv[argvc][argc][pos++] = ' ';
			}
			argvv[argvc][argc][pos-1] = '\0';
			break;
		case WRDE_NOSPACE:
			wordfree(&result);
		default:  
			perror("Expansión");     //otro error      
			return 1;
	}
	wordfree(&result);
	return 0;
}

//Auxiliar: se utiliza para cuando se quiere poner como variable de entorno un número
//			se pasa el número a string

void misetenv(char *var, int num) {
	char buf[32];
	sprintf(buf, "%d", num);
	setenv(var, buf, 1);
}