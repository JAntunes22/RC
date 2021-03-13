all: AS FS User PD
	
AS: AS.c
	gcc -Wall AS.c utils.c -o AS
FS: FS.c
	gcc -Wall FS.c utils.c -o FS
User: User.c
	gcc -Wall User.c utils.c -o user
PD: PD.c
	gcc -Wall PD.c utils.c -o pd
