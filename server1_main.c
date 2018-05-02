/*
 *PIERO CAVALCANTI s252117
 *
 */

#define OK_	"+OK\r\n"
#define QUIT_	"QUIT\r\n"
#define ERR_	"-ERR\r\n"
#define GET_	"GET "

#include <stdlib.h> // getenv()
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h> // timeval
#include <sys/select.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h> // inet_aton()
#include <sys/un.h> // unix sockets
#include <netdb.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
//#include <inttypes.h> // SCNu16

#include "../sockwrap.h"
#include "../errlib.h"


char *prog_name;
//Funzioni 

/*SelectReadTen: funzione che effettua una SELECT di 10 sec
sulle Read
Return = valore della SELECT, ovvero:
	 0 -> timeout raggiunto senza operazioni selezionate
	>0 -> numero complessivo di socket selezionati 
*/
int SelectReadTen(int s){
	
		fd_set cset;
		struct timeval tval;
		FD_ZERO(&cset);
		FD_SET(s,&cset);
		tval.tv_sec = 10;
		tval.tv_usec = 0;
		return select(s+1,&cset,NULL,NULL,&tval);
}


/*InvioFileServer: serve per inviare file verso il client.
ReturnValue:	1 = successfull
		<0 -> ERRORE 
		(-1 per errore apertura file,
		 -2 per errore nella write)
		*/
int InvioFileServer(int s1, char* nome_file){
	
			struct stat info_file;		
			FILE *f;
			
			int ret = stat(nome_file, &info_file);
			if (ret != 0) {
				err_msg("SERVER: ERRORE - stat non aperto\n");
				send (s1, ERR_, strlen(ERR_), 0);
				return -1;
			}
			//printf("DEBUG: stats aperto\n");
			f=fopen(nome_file, "rb");
			if ( f == NULL) {
				err_msg("SERVER: ERRORE - file non aperto\n");
				send(s1, ERR_, strlen(ERR_),0);
				return -1;
			}
			//printf("DEBUG: file aperto\n");
			int dimensione = info_file.st_size;
			send (s1, OK_,strlen(OK_),0);
			//printf("SERVER: Write of '%s' (dim=%lu)\n",OK_,strlen(OK_));
			uint32_t val = htonl(dimensione);
			send (s1, &val, 4,0);
			//printf("SERVER: Write of '%d' (dim=4)\n",dimensione);
			val= htonl(info_file.st_mtime);
			send (s1, &val, 4,0);
			//printf("SERVER: Write of '%ld' (dim=4)\n",info_file.st_mtime);
			char buffer[1024];
			int byte_letti, byte_trasf=0;
			int count_t=0;
			int byte_rimasti=dimensione;
			err_msg("SERVER: ---");
			err_msg("SERVER: TRASMISSIONE IN CORSO (buckets da 1024bytes)");
			do{
				
				byte_letti=fread(buffer, 1, 1024, f);
				byte_trasf=send(s1, buffer, byte_letti,0);
				//err_msg("DEBUG: letti=%d, inviati=%d",byte_letti,byte_trasf);
				
					
				if(byte_trasf<0){
					err_msg("SERVER: ERRORE - Write=-1\n");
					return -2;
				}
				byte_rimasti-=byte_trasf;
				count_t++;
				if(count_t<10 || byte_rimasti<=1024)err_msg("SERVER: Pacchetto#%d (%dB/%uB rimasti)             ", count_t, byte_rimasti, dimensione);
				if(count_t==10)err_msg("SERVER: ...");
			

			}while (/*r!=0*/byte_rimasti>0);
			err_msg("SERVER: ---");

			fclose(f);
			return 0;

}

/*CtrlCapServer, gestisce la ricezione dei messaggi di controllo iniziali 
rilevo i caratteri GET (tutto quello che verrà poi sarà il nome del file)
Return: 
	0 = "QUIT"
	>0 = "GET"
	<0 = Errore (o timeout select) o nessun messaggio identificato
*/
int CtrlCapServer(int s1){
		
		char ctrl_msg[8];
		char rec_b;
		int res_r=0;
		int count_r=0;

		err_msg("SERVER: (CtrlCapServer) Ricezione comando dal client...");
		memset(ctrl_msg,'\0',8);

		do{
			if(SelectReadTen(s1) > 0) res_r = read(s1, &rec_b,1);//res_r= risultato read
			else break;

			if (res_r==1) {
			    ctrl_msg[count_r++]=rec_b;
			    //printf("SERVER: identifico richiesta da client (msg='%s')\n",ctrl_msg);
			}
			else break;	
		}while((strncmp(ctrl_msg,GET_,sizeof(GET_))!=0 && strncmp(ctrl_msg,QUIT_,sizeof(QUIT_))!=0));

		//CONTROLLO MESSAGGIO
		if(strncmp(ctrl_msg,QUIT_,sizeof(QUIT_))==0)return 0;
		if(strncmp(ctrl_msg,GET_,sizeof(GET_))>=0)return 1;
		return -1;

}

/*NameCapServer, gestisce la ricezione dei messaggi per il nome del file
Return: 
	
	>0 = "OK"
	<0 = Errore o nessun messaggio identificato
*/
//SECONDA RICEZIONE: ricevo il nome del file (compreso di /n/r)
void NameCapServer(int s1, char* nome_file){

			char rec_msg[263];
			char rec_b;
			int count_r=0;

			err_msg("SERVER: (NameCapServer) GET ricevuto, ricezione del nome del file in corso...\n");
			
			int res_r;
			do{
				if(SelectReadTen(s1)>0) res_r = Read(s1, &rec_b,1);
				else {
					err_msg("SERVER: (NameCapServer)	ERRORE! Timeout raggiunto: impossibile completare ricezione del nome del file\n");
					break;
				}
				if (res_r==1) {

					if(rec_b != '\r' && rec_b != '\n'){
						rec_msg[count_r++]=rec_b;
						rec_msg[count_r]='\0';//per migliorare la printf
					}
					
					
				}
				//printf("(Read#%d,'%c',<%s>) ",count_r,rec_b,rec_msg);
			}while (rec_b != '\n' && count_r<=263);

			strcpy(nome_file, rec_msg);
}

//ServeFileServer
/*Funzione principale del server.
Return:
	<0 -> Errors
	>0 -> Successfully served
	=0 -> Quit*/
int ServeFileServer (int s1) {

	char nome_file[260];
	int ctrl=CtrlCapServer(s1);
	int invio_file=0;

	if(ctrl>0){//significa che riceve la GET
		NameCapServer(s1,nome_file);	
		invio_file=InvioFileServer(s1, nome_file);
		return 1;
	}
	else if(ctrl==0){
		err_msg("SERVER: (ServeFileServer) QUIT ricevuta, mi rimetto in ascolto...\n\n\n*****************************\n\n");
		return 0;
	}
	else{
		err_msg("SERVER: (ServeFileServer) ERRORE nel controllo dei caratteri\n");
		send (s1, ERR_, strlen(ERR_),0);
		return -1;
	}	
		
		
}

int GetSocket(char* porta){
	int s;
	struct sockaddr_in temp;
	s = Socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	printf("SERVER: (Socket - %d) Socket creata\n",s);
	temp.sin_family=AF_INET;
  	temp.sin_port = htons(atoi(porta));
  	temp.sin_addr.s_addr= htonl(INADDR_ANY);

	if(temp.sin_port<0 || temp.sin_port>65535){
	printf("ERRORE, porta invalida\n");
		return -1;
	}
	
	Bind(s, (struct sockaddr*) &temp, sizeof(temp));
	printf("SERVER: (Bind - %d) Binding...\n",s);

	return s;
}


int main (int argc, char *argv[])
{
	prog_name= argv[0];
	char* porta = argv[1];
	struct sockaddr_in ctemp;
	int s,s1,backlog=0;
	socklen_t addrlen=sizeof(ctemp);

	//CONTROLLO PARAMETRI
	if (argc<2){
		printf("ERRORE, necessari parametri: <destinazione> <porta> <file1> <file2> .. <fileN>\n");
		return -1;
	}

	//SOCKET
	if((s = GetSocket(porta))<0) return -1;
	
	//BIND
	

	//LISTEN
	Listen(s, backlog);
	printf("SERVER: (Listen - %d) In ascolto sulla porta <%s>, in attesa di connessione.. \n", s, porta);

	//ACCEPT, SERVE & CLOSE (in ciclo infinito)
	while (1) {
		s1 = Accept (s, (struct sockaddr*) &ctemp, &addrlen);
		if(s1 < 0) continue;
		err_msg("SERVER: (Accept - %d) Connesso al client (%s:%u)",s1,inet_ntoa(ctemp.sin_addr), (unsigned) ntohs(ctemp.sin_port));

		//SERVE
		while(ServeFileServer(s1)>0);
		
		if(close(s1) != 0){
			err_msg("SERVER: (Close - %d) Impossibile chiudere Socket",s1);
			return -1;
		}
	}
	system("PAUSE");
	return 0;
}
