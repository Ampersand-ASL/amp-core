#include <alsa/asoundlib.h>

int snd_pcm_prepare(snd_pcm_t *pcm) {
    return 0;    
}

int	snd_pcm_open(snd_pcm_t **pcm, const char *name, enum snd_pcm_stream_t stream, int mode) {
    return 0;
}

snd_pcm_sframes_t snd_pcm_writei (snd_pcm_t *pcm, const void *buffer, snd_pcm_uframes_t size) {
    return 0;
}

snd_pcm_sframes_t snd_pcm_readi(snd_pcm_t* pcm, void* buffer,snd_pcm_uframes_t size ) {
    return 0;
}

int snd_pcm_close(snd_pcm_t *pcm) {
    return 0;
}
int	snd_pcm_hw_params_current(snd_pcm_t *pcm, snd_pcm_hw_params_t *params) { 
    return 0;
}
int snd_pcm_hw_params(snd_pcm_t* pcm, snd_pcm_hw_params_t* params) { 
    return 0;
}
int	snd_pcm_hw_params_set_channels_near (snd_pcm_t *pcm, snd_pcm_hw_params_t *params, unsigned int *val) { 
    return 0;
}
int	snd_pcm_hw_params_set_rate_near(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, unsigned int *val, int *dir) { 
    return 0;
}
int snd_pcm_hw_params_set_format(snd_pcm_t*	pcm, snd_pcm_hw_params_t* params, enum snd_pcm_format_t format) { 
    return 0;
}
int	snd_pcm_hw_params_set_access (snd_pcm_t *pcm, snd_pcm_hw_params_t *params, enum snd_pcm_access_t _access) { 
    return 0;
}
int	snd_pcm_hw_params_any (snd_pcm_t *pcm, snd_pcm_hw_params_t *params) { 
    return 0;
}


