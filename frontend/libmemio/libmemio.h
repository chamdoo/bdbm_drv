#include "bdbm_drv.h"
#include "umemory.h" /* bdbm_malloc */
#include "devices.h" /* bdbm_dm_get_inf */
#include "debug.h" /* bdbm_msg */

typedef struct memio {
	bdbm_drv_info_t bdi;
	bdbm_llm_req_t* rr;
	int nr_punits;
	size_t io_size; /* bytes */
	size_t trim_size;
	size_t trim_lbas;
} memio_t;

memio_t* memio_open ();
void memio_wait (memio_t* mio);
int memio_read (memio_t* mio, size_t lba, size_t len, uint8_t* data);
int memio_write (memio_t* mio, size_t lba, size_t len, uint8_t* data);
int memio_trim (memio_t* mio, size_t lba, size_t len);
void memio_close (memio_t* mio);
