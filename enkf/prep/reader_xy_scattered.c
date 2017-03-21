/******************************************************************************
 *
 * File:        reader_viirs.c        
 *
 * Created:     08/03/2017
 *
 * Author:      Pavel Sakov
 *              Bureau of Meteorology
 *
 * Description: Reader for pre-preprocessed (L3C) SST from VIIRS.
 *
 * Revisions:  
 *
 *****************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include "ncw.h"
#include "nan.h"
#include "definitions.h"
#include "utils.h"
#include "obsmeta.h"
#include "model.h"
#include "grid.h"
#include "observations.h"
#include "prep_utils.h"

void reader_xy_scattered(char* fname, int fid, obsmeta* meta, model* m, observations* obs)
{
    char* varname = NULL;
    char* lonname = NULL;
    char* latname = NULL;
    char* stdname = NULL;
    char* estdname = NULL;
    char* timename = NULL;
    int ncid;
    int ndim;
    int dimid_nobs;
    size_t nobs;

    int varid_var = -1, varid_lon = -1, varid_lat = -1, varid_std = -1, varid_estd = -1, varid_time = -1;
    double* lon = NULL;
    double lon_add_offset, lon_scale_factor;
    double lon_fill_value = NaN;
    double* lat = NULL;
    double lat_add_offset, lat_scale_factor;
    double lat_fill_value = NaN;
    double* var = NULL;
    double var_shift = 0.0;
    double var_fill_value = NaN;
    double var_add_offset, var_scale_factor;
    double* std = NULL;
    double std_add_offset, std_scale_factor;
    double std_fill_value = NaN;
    double* estd = NULL;
    double estd_add_offset, estd_scale_factor;
    double estd_fill_value = NaN;
    double* time = NULL;
    double time_add_offset, time_scale_factor;
    double time_fill_value = NaN;
    char tunits[MAXSTRLEN];
    double tunits_multiple, tunits_offset;
    int mvid;
    float** depth;
    int ktop;
    int i, nobs_read;

    for (i = 0; i < meta->npars; ++i) {
        if (strcasecmp(meta->pars[i].name, "VARNAME") == 0)
            varname = meta->pars[i].value;
        else if (strcasecmp(meta->pars[i].name, "LONNAME") == 0)
            lonname = meta->pars[i].value;
        else if (strcasecmp(meta->pars[i].name, "LATNAME") == 0)
            latname = meta->pars[i].value;
        else if (strcasecmp(meta->pars[i].name, "STDNAME") == 0)
            stdname = meta->pars[i].value;
        else if (strcasecmp(meta->pars[i].name, "ESTDNAME") == 0)
            estdname = meta->pars[i].value;
        else if (strcasecmp(meta->pars[i].name, "VARSHIFT") == 0) {
            if (!str2double(meta->pars[i].value, &var_shift))
                enkf_quit("observation prm file: can not convert VARSHIFT = \"%s\" to double\n", meta->pars[i].value);
            enkf_printf("        VARSHIFT = %.0f\n", var_shift);
        } else
            enkf_quit("unknown PARAMETER \"%s\"\n", meta->pars[i].name);
    }
    if (varname == NULL)
        enkf_quit("reader_xy_scattered(): %s variable name not specified", fname);

    ncw_open(fname, NC_NOWRITE, &ncid);
    ncw_inq_varid(ncid, varname, &varid_var);
    ncw_inq_varndims(ncid, varid_var, &ndim);
    if (ndim != 1)
        enkf_quit("reader_xy_scattered(): %s: # dimensions = %d (must be 1)", fname, ndim);

    ncw_inq_dimid(ncid, "nobs", &dimid_nobs);
    ncw_inq_dimlen(ncid, dimid_nobs, &nobs);

    if (lonname != NULL)
        ncw_inq_varid(ncid, lonname, &varid_lon);
    else if (ncw_var_exists(ncid, "lon"))
        ncw_inq_varid(ncid, "lon", &varid_lon);
    else if (ncw_var_exists(ncid, "longitude"))
        ncw_inq_varid(ncid, "longitude", &varid_lon);
    else
        enkf_quit("reader_xy_scattered(): %s: could not find longitude variable", fname);
    
    if (latname != NULL)
        ncw_inq_varid(ncid, latname, &varid_lat);
    else if (ncw_var_exists(ncid, "lat"))
        ncw_inq_varid(ncid, "lat", &varid_lat);
    else if (ncw_var_exists(ncid, "latitude"))
        ncw_inq_varid(ncid, "latitude", &varid_lat);
    else
        enkf_quit("reader_xy_scattered(): %s: could not find latitude variable", fname);
    
    lon = malloc(nobs * sizeof(double));
    lat = malloc(nobs * sizeof(double));
    ncw_get_var_double(ncid, varid_lon, lon);
    ncw_get_var_double(ncid, varid_lat, lat);
    if (ncw_att_exists(ncid, varid_lon, "_FillValue")) {
        ncw_get_att_double(ncid, varid_lon, "_FillValue", &lon_fill_value);
        ncw_get_att_double(ncid, varid_lat, "_FillValue", &lat_fill_value);
    }
    if (ncw_att_exists(ncid, varid_lon, "add_offset")) {
        ncw_get_att_double(ncid, varid_lon, "add_offset", &lon_add_offset);
        ncw_get_att_double(ncid, varid_lon, "scale_factor", &lon_scale_factor);
        ncw_get_att_double(ncid, varid_lat, "add_offset", &lat_add_offset);
        ncw_get_att_double(ncid, varid_lat, "scale_factor", &lat_scale_factor);

        for (i = 0; i < nobs; ++i)
            if (lon[i] != lon_fill_value)
                lon[i] = lon[i] * lon_scale_factor + lon_add_offset;
        for (i = 0; i < nobs; ++i)
            if (lat[i] != lat_fill_value)
                lat[i] = lat[i] * lat_scale_factor + lat_add_offset;
    }


    var = malloc(nobs * sizeof(double));
    ncw_get_var_double(ncid, varid_var, var);
    if (ncw_att_exists(ncid, varid_var, "_FillValue"))
        ncw_get_att_double(ncid, varid_var, "_FillValue", &var_fill_value);
    if (ncw_att_exists(ncid, varid_var, "add_offset")) {
        ncw_get_att_double(ncid, varid_var, "add_offset", &var_add_offset);
        ncw_get_att_double(ncid, varid_var, "scale_factor", &var_scale_factor);

        for (i = 0; i < nobs; ++i)
            if (var[i] != var_fill_value)
                var[i] = var[i] * var_scale_factor + var_add_offset;
    }

    if (stdname != NULL)
        ncw_inq_varid(ncid, stdname, &varid_std);
    else if (ncw_var_exists(ncid, "std"))
        ncw_inq_varid(ncid, "std", &varid_std);
    else
        enkf_quit("reader_xy_scattered(): %s: could not find STD variable", fname);
    std = malloc(nobs * sizeof(double));
    ncw_get_var_double(ncid, varid_std, std);
    if (ncw_att_exists(ncid, varid_std, "_FillValue"))
        ncw_get_att_double(ncid, varid_std, "_FillValue", &std_fill_value);
    if (ncw_att_exists(ncid, varid_std, "add_offset")) {
        ncw_get_att_double(ncid, varid_std, "add_offset", &std_add_offset);
        ncw_get_att_double(ncid, varid_std, "scale_factor", &std_scale_factor);

        for (i = 0; i < nobs; ++i)
            if (std[i] != std_fill_value)
                std[i] = std[i] * std_scale_factor + std_add_offset;
    }

    if (estdname != NULL)
        ncw_inq_varid(ncid, estdname, &varid_estd);
    else if (ncw_var_exists(ncid, "error_std"))
        ncw_inq_varid(ncid, "error_std", &varid_estd);
    else
        enkf_quit("reader_xy_scattered(): %s: could not find ERROR_STD variable", fname);
    estd = malloc(nobs * sizeof(double));
    ncw_get_var_double(ncid, varid_estd, estd);
    if (ncw_att_exists(ncid, varid_estd, "_FillValue"))
        ncw_get_att_double(ncid, varid_estd, "_FillValue", &estd_fill_value);
    if (ncw_att_exists(ncid, varid_estd, "add_offset")) {
        ncw_get_att_double(ncid, varid_estd, "add_offset", &estd_add_offset);
        ncw_get_att_double(ncid, varid_estd, "scale_factor", &estd_scale_factor);

        for (i = 0; i < nobs; ++i)
            if (estd[i] != estd_fill_value)
                estd[i] = estd[i] * estd_scale_factor + estd_add_offset;
    }

    if (timename != NULL)
        ncw_inq_varid(ncid, timename, &varid_time);
    else if (ncw_var_exists(ncid, "time"))
        ncw_inq_varid(ncid, "time", &varid_time);
    else
        enkf_quit("reader_xy_scattered(): %s: could not find TIME variable", fname);
    time = malloc(nobs * sizeof(double));
    ncw_get_var_double(ncid, varid_time, time);
    if (ncw_att_exists(ncid, varid_time, "_FillValue"))
        ncw_get_att_double(ncid, varid_time, "_FillValue", &time_fill_value);
    if (ncw_att_exists(ncid, varid_time, "add_offset")) {
        ncw_get_att_double(ncid, varid_time, "add_offset", &time_add_offset);
        ncw_get_att_double(ncid, varid_time, "scale_factor", &time_scale_factor);

        for (i = 0; i < nobs; ++i)
            if (time[i] != time_fill_value)
                time[i] = time[i] * time_scale_factor + time_add_offset;
    }
    ncw_get_att_text(ncid, varid_time, "units", tunits);
    tunits_convert(tunits, &tunits_multiple, &tunits_offset);

    ncw_close(ncid);

    mvid = model_getvarid(m, obs->obstypes[obstype_getid(obs->nobstypes, obs->obstypes, meta->type, 1)].varnames[0], 1);
    ktop = grid_gettoplayerid(model_getvargrid(m, mvid));
    depth = model_getdepth(m, mvid, 0);

    nobs_read = 0;
    for (i = 0; i < nobs; ++i) {
        observation* o;
        obstype* ot;

        if (lon[i] == lon_fill_value || lat[i] == lat_fill_value || var[i] == var_fill_value || std[i] == std_fill_value || estd[i] == estd_fill_value || time[i] == time_fill_value)
            continue;

        nobs_read++;
        obs_checkalloc(obs);
        o = &obs->data[obs->nobs];

        o->product = st_findindexbystring(obs->products, meta->product);
        assert(o->product >= 0);
        o->type = obstype_getid(obs->nobstypes, obs->obstypes, meta->type, 1);
        ot = &obs->obstypes[o->type];
        o->instrument = st_add_ifabscent(obs->instruments, "VIIRS", -1);
        o->id = obs->nobs;
        o->fid = fid;
        o->batch = 0;
        o->value = var[i] + var_shift;
        o->std = (std[i] > estd[i]) ? std[i] : estd[i];
        o->lon = lon[i];
        o->lat = lat[i];
        o->depth = 0.0;
        o->fk = (double) ktop;
        o->status = model_xy2fij(m, mvid, o->lon, o->lat, &o->fi, &o->fj);
        if (!obs->allobs && o->status == STATUS_OUTSIDEGRID)
            continue;
        if ((o->status == STATUS_OK) && (o->lon <= ot->xmin || o->lon >= ot->xmax || o->lat <= ot->ymin || o->lat >= ot->ymax))
            o->status = STATUS_OUTSIDEOBSDOMAIN;
        o->model_depth = (depth == NULL || isnan(o->fi + o->fj)) ? NaN : depth[(int) (o->fj + 0.5)][(int) (o->fi + 0.5)];
        o->date = time[i] * tunits_multiple + tunits_offset;
        o->aux = -1;

        obs->nobs++;
    }
    enkf_printf("        nobs = %d\n", nobs_read);

    free(lon);
    free(lat);
    free(var);
    free(std);
    free(estd);
    free(time);
}