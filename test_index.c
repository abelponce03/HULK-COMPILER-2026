#include <stdio.h>
int main() {
    char alphabet[128];
    int size = 0;
    for (int c = 32; c < 127; c++) alphabet[size++] = (char)c;
    alphabet[size++] = '\t';
    alphabet[size++] = '\n';
    alphabet[size++] = '\r';
    
    printf("alphabet_size = %d\n", size);
    
    // Buscar índice de 'l'
    for (int i = 0; i < size; i++) {
        if (alphabet[i] == 'l') {
            printf("'l' está en índice %d, alphabet[%d]='%c'\n", i, i, alphabet[i]);
            break;
        }
    }
    
    // También mostrar algunos índices
    printf("alphabet[0]='%c' (%d)\n", alphabet[0], alphabet[0]);
    printf("alphabet[76]='%c' (%d)\n", alphabet[76], alphabet[76]);
    return 0;
}
