/*
 * TEMPLATE CLIENT - S252117
 */

#include <stdlib.h> // getenv()
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h> // timeval
#include <sys/select.h>
#include <sys/wait.h>
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




#define OK_	"+OK\r\n"
#define QUIT_	"QUIT\r\n"
#define ERR_	"-ERR\r\n"



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



/*SocketfromAddr: effettua una serie di operazioni, tra cui controllo delle porte, risolutore di indirizzi, Socket & Connect.
Return:	(s = valore di socket) =
	<0 -> fallimento
	>0 -> socket &
		*/
int SocketfromAddr(const char* destinazione, const char* porta){
	//PORTA
	int p= atoi(porta);
	int s;
	char* prob;
	if (p<0 || p>65535)err_quit("ERRORE! Porta non valida");	
	
	//HOST
	struct addrinfo *d, *d_list, hints;
	memset(&hints,0,sizeof(hints));

	hints.ai_family = AF_UNSPEC;		//AF_UNSPEC = IPV4 OR IPV6
	hints.ai_socktype = SOCK_STREAM;	//TCP
	hints.ai_protocol = IPPROTO_TCP;	//TCP
	hints.ai_flags = AI_V4MAPPED | AI_ADDRCONFIG;	//CONSIGLIATO PER MIGLIORARE LA SPECIFICA
	
	Getaddrinfo(destinazione,porta,&hints, &d);

	for(d_list=d; d_list != NULL; d_list=d_list->ai_next){

	s= Socket(d_list->ai_family, d_list->ai_socktype, d_list->ai_protocol);
	if(s<0){
		prob="ERRORE (SOCKET)!";
		continue;	
	}
	if(connect(s,d_list->ai_addr,d_list->ai_addrlen)<0){
		prob="ERRORE (CONNECT)!";
		s=-1;
		continue;
	}
	break; //INDIRIZZO CORRETTO TROVATO
	}

	if(s<0)err_msg("CLIENT:%s",prob);
	else err_msg("CLIENT: (Socket = %d) Socket creata",s);

	freeaddrinfo(d);
	return s;
};

int RecvFileClient(int socket,const char* nomefile){
	//PROCEDIMENTO

	err_msg("CLIENT: RECV: parametri passati: socket=%d, nomefile='%s' \n",socket,nomefile);
  	
	//1 Ricezione di dimensione = 4byte(B1-B2-B3-B4) 
	uint32_t dimensione=0;
	Readn(socket,&dimensione,4);
	dimensione = ntohl(dimensione);
	printf("CLIENT: Dimensione File: %ubytes\n",dimensione);

	//2 Ricezione di timestamp unix uint32_t
	uint32_t timestamp=0;
	Readn(socket,&timestamp,4);
	timestamp = ntohl(timestamp);
	err_msg("CLIENT: Timestamp (Unix Time) relativo a file: %usec dall'ultima epoca",timestamp);
	

	//3 Ricezione del file
	FILE *f=fopen(nomefile, "wb");
	if(f==NULL){
		err_msg("CLIENT: ERRORE: impossibile accedere al File");
		return(-1);
	}
	char buffer[1024];
	int byte_trasf=0;
	int totale_bytes=0;
	int count_t=0;
	err_msg("\nCLIENT: ---");
	err_msg("CLIENT: RICEZIONE IN CORSO (buckets da (massimo) 1024bytes)");

	while(totale_bytes<dimensione){

		if(dimensione-totale_bytes<1024){
			byte_trasf=Readn(socket,buffer,dimensione-totale_bytes);		
		}
		else{
			byte_trasf=Readn(socket,buffer,sizeof(buffer));
		}
		totale_bytes+=byte_trasf;
		count_t++;
		if(count_t<10 || dimensione-totale_bytes<1024)err_msg("CLIENT: Pacchetto#%d (%dB/%uB ricevuti)", count_t, totale_bytes, dimensione);
		if(count_t==10)err_msg("CLIENT: ...");
		fwrite(buffer,sizeof(char),byte_trasf,f);
	}
	err_msg("CLIENT: ---");

	if(!fclose(f))err_msg("CLIENT: File chiuso");
	else err_quit("CLIENT: ERRORE! impossibile chiudere il file");
	return totale_bytes;
};

void CloseConn(int s){
	Sendn(s,QUIT_,6,0);
	err_msg("CLIENT: inviato segnale d'arresto: 'QUIT '");
	Close(s);
};







int main (int argc, char *argv[])
{
	if (argc<4){
		err_msg("CLIENT: ERRORE, necessari parametri: <destinazione> <porta> <file1> <file2> .. <fileN>\n");
		return -1;
	}	

	//Gestione Comunicazione C/S
	char nome_file[260];					//dimensione massima path = 260 (windows, Linux è 255)	
	char msg[266];
	char ctrl_msg[8];

	int s;
	int result=0;
	int count_r=0;
	char rec_b;
	int res_r;

	//Parametri da programma:	
	prog_name= argv[0];
	char* destinazione = argv[1];
	char* porta = argv[2];


	s=SocketfromAddr(destinazione,porta);			//SocketfromAddr effettua SOCKET & CONNECT


	for(int i=3;i<argc;i++){

		if(strlen(argv[i])>255){
			err_msg("CLIENT: ERRORE! nome file invalido, proseguo con argomento successivo..");
			continue;
		}

		//SEND (di GETxxx\r\n):
		memcpy(nome_file,argv[i],strlen(argv[i])+1);	//copia del nome del file i-esimo
		sprintf(msg,"GET %s\r\n",nome_file);		//copia della stringa GETxxxCRLF in msg		
		Sendn (s, msg, strlen(msg),0);			//sending di msg
		
		err_msg("Ricezione 1");
		if(SelectReadTen(s) > 0){			//SelectReadTen esegue una select di 10s
		//1a RICEZIONE
			res_r=0;
			count_r=0;
			memset(ctrl_msg,'\0',sizeof(ctrl_msg));		
			do{
			    res_r=Read(s,&rec_b,sizeof(char));
			    if(res_r==1)ctrl_msg[count_r++]=rec_b;
			    if(rec_b!='\n'&& rec_b!='\r')err_msg("(%c)",rec_b,ctrl_msg);	
			} while(ctrl_msg[count_r-1]!='\n' && count_r<=6);
			ctrl_msg[count_r]='\0';
	
			if(strncmp(ctrl_msg,OK_,6)==0){
			    result=RecvFileClient(s,nome_file);
			    err_msg("CLIENT: Trasferimento eseguito: %d bytes trasferiti per file '%s'",result,nome_file);	
			}
			else err_msg("CLIENT: ERRORE: segnalazione da server='%s'",ctrl_msg);
		}else err_msg("CLIENT: ERRORE: (10 sec) timeout raggiunto",ctrl_msg);
	}
	
		//CHIUSURA CONNESSIONE
		CloseConn(s);
		return 0;	
}
