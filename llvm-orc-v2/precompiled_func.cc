extern "C" {

int id(int x) { return x;}

int add1(int x) { return x+1;}

int times2(int x) { return 2*x;}
// for functions above, it's natural to expect the JIT could do the best optimization,
// and for the following let's see how the JIT behaves. 
int squareroot(int x) {
    int i = 1;
    while ((i+1)*(i+1) <= x) ++i;
    return i;
}

int threeX_1(int x) {
    int cnt = 0;
    while (x != 1) {
        if (x&1) x = 3*x + 1;
        else x /= 2;
        ++cnt;
    }
    return cnt;
}
}