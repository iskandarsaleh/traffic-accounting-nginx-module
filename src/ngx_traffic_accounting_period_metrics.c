
/*
 * Copyright (C) Liu Lantao
 */


#include "ngx_traffic_accounting.h"


ngx_traffic_accounting_period_t   *ngxta_current_metrics;
ngx_traffic_accounting_period_t   *ngxta_previous_metrics;

static void ngxta_period_rbtree_insert_value(ngx_rbtree_node_t *temp, ngx_rbtree_node_t *node, ngx_rbtree_node_t *sentinel);

ngx_int_t
ngxta_period_rbtree_init(ngx_traffic_accounting_period_t *period)
{
    ngx_rbtree_init(&period->rbtree, &period->sentinel,
                    ngxta_period_rbtree_insert_value);

    return NGX_OK;
}

void
ngxta_period_rbtree_insert(ngx_traffic_accounting_period_t *period,
    ngx_str_t *name)
{
    ngx_traffic_accounting_metrics_t   *metrics;

    metrics = ngx_pcalloc(period->pool, sizeof(ngx_traffic_accounting_metrics_t));

    void *data;
    data = ngx_pcalloc(period->pool, name->len+1);
    ngx_memcpy(data, name->data, name->len);

    metrics->name.data = data;
    metrics->name.len = name->len;

    ngxta_period_rbtree_insert_metrics(period, metrics);
}

void
ngxta_period_rbtree_insert_metrics(ngx_traffic_accounting_period_t *period, ngx_traffic_accounting_metrics_t *metrics)
{
    ngx_rbtree_t *rbtree = &period->rbtree;
    ngx_rbtree_node_t *node = &metrics->rbnode;

    node->key = ngx_hash_key_lc(metrics->name.data, metrics->name.len);

    ngx_rbtree_insert(rbtree, node);
}

void
ngxta_period_rbtree_delete(ngx_traffic_accounting_period_t *period, ngx_str_t *name)
{
    ngx_traffic_accounting_metrics_t   *metrics;

    metrics = ngxta_period_rbtree_lookup_metrics(period, name);
    if (metrics == NULL)
      return;

    ngxta_period_rbtree_delete_metrics(period, metrics);
}

void
ngxta_period_rbtree_delete_metrics(ngx_traffic_accounting_period_t *period, ngx_traffic_accounting_metrics_t *metrics)
{
    ngx_rbtree_delete(&period->rbtree, &metrics->rbnode);
    ngx_pfree(period->pool, metrics);
}

ngx_traffic_accounting_metrics_t *
ngxta_period_rbtree_lookup_metrics(ngx_traffic_accounting_period_t *period, ngx_str_t *name)
{
    ngx_int_t           rc;
    ngx_traffic_accounting_metrics_t   *n;
    ngx_rbtree_node_t  *node, *sentinel;

    ngx_rbtree_key_t hash = ngx_hash_key_lc(name->data, name->len);

    ngx_rbtree_t *rbtree = &period->rbtree;
    node = rbtree->root;
    sentinel = rbtree->sentinel;

    while (node != sentinel) {
        if (hash != node->key) {
            node = (hash < node->key) ? node->left : node->right;
            continue;
        }

        n = (ngx_traffic_accounting_metrics_t *) node;
        rc = ngx_rstrncmp(name->data, n->name.data, name->len);

        if (rc < 0) {
            node = node->left;
            continue;
        }

        if (rc > 0) {
            node = node->right;
            continue;
        }

        return n;
    }

    return NULL;
}

ngx_int_t
ngxta_period_rbtree_iterate(ngx_traffic_accounting_period_t *period,
                            ngxta_period_rbtree_iterate_func func,
                            void *para1, void *para2 )
{
    ngx_traffic_accounting_metrics_t   *n;
    ngx_rbtree_node_t  *node, *sentinel;
    ngx_int_t           ret_code;

    ngx_rbtree_t *rbtree = &period->rbtree;
    node = rbtree->root;
    sentinel = rbtree->sentinel;

    while (node != sentinel) {
        n = (ngx_traffic_accounting_metrics_t *) node;
        ret_code = func(n, para1, para2);

        if (ret_code != NGX_OK)
            return ret_code;

        // squeeze!
        ngx_rbtree_delete(rbtree, node);
        ngx_pfree(period->pool, n->nr_statuses);
        ngx_pfree(period->pool, n);

        node = rbtree->root;
    }

    return NGX_OK;
}

static void
ngxta_period_rbtree_insert_value(ngx_rbtree_node_t *temp,
    ngx_rbtree_node_t *node, ngx_rbtree_node_t *sentinel)
{
    ngx_traffic_accounting_metrics_t   *n, *t; // node
    ngx_rbtree_node_t  **p;

    for ( ;; ) {
        n = (ngx_traffic_accounting_metrics_t *) node;
        t = (ngx_traffic_accounting_metrics_t *) temp;

        if (node->key != temp->key) {
            p = (node->key < temp->key) ? &temp->left : &temp->right;
        } else if (n->name.len != t->name.len) {
            p = (n->name.len < t->name.len) ? &temp->left : &temp->right;
        } else {
            p = (ngx_memcmp(n->name.data, t->name.data, n->name.len) < 0)
                 ? &temp->left : &temp->right;
        }

        if (*p == sentinel) {
            break;
        }

        temp = *p;
    }

    *p = node;
    node->parent = temp;
    node->left = sentinel;
    node->right = sentinel;
    ngx_rbt_red(node);
}


ngx_int_t
ngxta_period_init(ngx_pool_t *pool)
{
    ngx_traffic_accounting_period_t   *period = ngx_pcalloc(pool, sizeof(ngx_traffic_accounting_period_t));

    if (period != NULL) {
        period->pool = pool;
        ngxta_period_rbtree_init(period);

        period->created_at = ngx_timeofday();

        ngxta_current_metrics = period;
        return NGX_OK;
    }

    return NGX_ERROR;
}

ngx_int_t
ngxta_period_rotate(ngx_pool_t *pool)
{
    ngx_pfree(pool, ngxta_previous_metrics);

    ngxta_previous_metrics = ngxta_current_metrics;

    return ngxta_period_init(pool);
}
