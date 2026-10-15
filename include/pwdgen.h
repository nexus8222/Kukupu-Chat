#ifndef PWDGEN_H        // Protect against multiple inclusions
#define PWDGEN_H
#define MAX_TRIES 5
extern int pwdencrypt(char *pwd, char *enc_pwd);   // The external interface of file2.c
extern int pwddecrypt(char *enc_pwd, char *pwd);
#endif