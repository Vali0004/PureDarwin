extern void abort(void);

void thread_start(void)   { abort(); }
void start_wqthread(void) { abort(); }

typedef unsigned long pd_pthread_priority_t;
void _pthread_set_main_qos(pd_pthread_priority_t qos) { (void)qos; }
