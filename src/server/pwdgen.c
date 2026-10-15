int pwdencrypt(char *pwd, char *enc_pwd);
int pwddecrypt(char *enc_pwd, char *pwd);

int pwdencrypt(char *pwd, char *enc_pwd) {
	int len = 0; char t;
	for ( ; *(pwd + len) != '\0'; len++);
	for(int i = 0; i < len; i++) {
		*(enc_pwd + i) = *(pwd + i);
	}
	if (len % 2 == 0 ) {
		for (int i = 0; i < len; i = i + 4) {
			if ( i + 2 < len ) {
				t = *(enc_pwd + i);
				*(enc_pwd + i) = *(enc_pwd + i + 2);
				*(enc_pwd + i + 2) = t;
			}
		}
		for (int i = 1; i < len; i = i + 2) {
			*(enc_pwd + i) = (char) ( (int) *(enc_pwd+i) + i);
		}
	} else {
		for (int i = 1; i < len; i = i + 4) {
			if ( i + 2 < len ) {
				t = *(enc_pwd + i);
				*(enc_pwd + i) = *(enc_pwd + i + 2);
				*(enc_pwd + i + 2) = t;
			}
		}
		for (int i = 0; i < len; i = i + 2) {
			*(enc_pwd + i) = (char) ( (int) *(enc_pwd+i) + i);
		}
	}
	for (int i = 0; i < len/2; i++) {
		t = *(enc_pwd + i);
		*(enc_pwd + i) = *(enc_pwd + len - i - 1);
		*(enc_pwd + len - i - 1) = t;
	}
	int flag = 0;
	for (int i = 0; i < len; i++) {
		if( *(pwd + i) != *(enc_pwd + i) ) {
			flag = 1;
		}
	}
	*(enc_pwd + len) = '\0';
	return flag;
}


int pwddecrypt(char *enc_pwd, char *pwd) {
	int len = 0; char t;
	for ( ; *(enc_pwd + len) != '\0'; len++);
	for(int i = 0; i < len; i++) {
		*(pwd + i) = *(enc_pwd + i);
	}
	for (int i = 0; i < len/2; i++) {
		t = *(pwd + i);
		*(pwd + i) = *(pwd + len - i - 1);
		*(pwd + len - i - 1) = t;
	}
	if (len % 2 == 0 ) {
		for (int i = 0; i < len; i = i + 4) {
			if ( i + 2 < len ) {
				t = *(pwd + i);
				*(pwd + i) = *(pwd + i + 2);
				*(pwd + i + 2) = t;
			}
		}
		for (int i = 1; i < len; i = i + 2) {
			*(pwd + i) = (char) ( (int) *(pwd+i) - i);
		}
	} else {
		for (int i = 1; i < len; i = i + 4) {
			if ( i + 2 < len ) {
				t = *(pwd + i);
				*(pwd + i) = *(pwd + i + 2);
				*(pwd + i + 2) = t;
			}
		}
		for (int i = 0; i < len; i = i + 2) {
			*(pwd + i) = (char) ( (int) *(pwd+i) - i);
		}
	}
	int flag = 0;
	for (int i = 0; i < len; i++) {
		if( *(pwd + i) != *(enc_pwd + i) ) {
			flag = 1;
		}
	}
	*(pwd + len) = '\0';
	return flag;
}