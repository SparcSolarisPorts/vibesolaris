/* SPDX-License-Identifier: Unlicense */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int vs_cancel_requested(const VSContext *c){(void)c;return 0;}

static int node_named(const VSGraph *g,const char *name){int i;for(i=0;i<g->node_count;i++)if(!strcmp(g->nodes[i].label,name))return i;return -1;}
static int has_edge(const VSGraph *g,int a,int b){int i;for(i=0;i<g->edge_count;i++)if(g->edges[i].from==a&&g->edges[i].to==b)return 1;return 0;}

int main(void){
    char root[256],a[320],b[320];FILE *f;VSGraph fg,fields;VSContext c;int na,nb,sd,sx,sn;
    snprintf(root,sizeof(root),"/tmp/vibesolaris-graph-%ld",(long)getpid());mkdir(root,0700);
    snprintf(a,sizeof(a),"%s/a.c",root);snprintf(b,sizeof(b),"%s/b.h",root);
    f=fopen(a,"w");if(!f)return 2;fputs("#include \"b.h\"\nint main(void){Demo d; d.x=1; return d.x;}\n",f);fclose(f);
    f=fopen(b,"w");if(!f)return 2;fputs("typedef struct Demo {\n    int x;\n    const char *name;\n} Demo;\n",f);fclose(f);
    memset(&c,0,sizeof(c));
    if(vs_graph_build(&c,root,VS_GRAPH_FILES,&fg)<0)return 3;
    na=node_named(&fg,"a.c");nb=node_named(&fg,"b.h");
    if(na<0||nb<0||!has_edge(&fg,na,nb)){fprintf(stderr,"file graph missing include edge\n");return 4;}
    if(vs_graph_build(&c,root,VS_GRAPH_FIELDS,&fields)<0)return 5;
    sd=node_named(&fields,"Demo");sx=node_named(&fields,"Demo.x");sn=node_named(&fields,"Demo.name");
    if(sd<0||sx<0||sn<0||!has_edge(&fields,sd,sx)||!has_edge(&fields,sd,sn)){fprintf(stderr,"field graph missing struct fields\n");return 6;}
    printf("graph ok files=%d/%d fields=%d/%d\n",fg.node_count,fg.edge_count,fields.node_count,fields.edge_count);
    unlink(a);unlink(b);rmdir(root);return 0;
}
