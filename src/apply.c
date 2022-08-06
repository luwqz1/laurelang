#include "laurelang.h"
#include "laureimage.h"
#include "predpub.h"

#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <readline/readline.h>

#ifndef DOC_BUFFER_LEN
#define DOC_BUFFER_LEN 512
#endif

#define GENERIC_T "T"

short int LAURE_ASK_IGNORE = 0;
char     *NESTED_DOC_AUTOGEN = NULL;
Instance *_TEMP_PREDCONSULT_LAST = NULL;

void print_errhead(string str) {
    printf("  %s", RED_COLOR);
    printf("%s", str);
    printf("%s\n", NO_COLOR);
}

void print_header(string header, uint sz) {
    if (sz == 0) {
        struct winsize w;
        ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
        sz = w.ws_col;
    }
    uint side = (sz - strlen(header)) / 2;
    char spaces[128];
    memset(spaces, ' ', side);
    spaces[side-1] = 0;
    printf("%s", RED_COLOR);
    printf("%s%s%s", spaces, header, spaces);
    printf("%s\n", NO_COLOR);
}

void ask_for_halt() {
    if (LAURE_ASK_IGNORE) return;
    string line = readline("Should consulting be continued (use yy to ignore asking further)? Y/yy/n: ");
    short int mode;
    if (strlen(line) == 0 || str_eq(line, "Y")) mode = 1;
    else if (str_eq(line, "yy")) mode = 2;
    else if (str_eq(line, "n")) exit(6);
    else {
        laure_free(line);
        ask_for_halt();
        return;
    }
    laure_free(line);
    if (mode) {
        if (mode == 2) LAURE_ASK_IGNORE = true;
        return;
    }
}

apply_result_t respond_apply(apply_status_t status, string e) {
    apply_result_t ar;
    ar.status = status;
    ar.error = e;
    return ar;
};

int laure_load_shared(laure_session_t *session, char *path) {
    void *lib = dlopen(path, RTLD_NOW);

    // Shared CLE (C logic environment) extension
    if (!lib) {
        print_errhead("failed to load shared CLE extension");
        string error = dlerror();
        if (strstr(error, "no such file"))
            strcpy(error, "Shared object is undefined");
        printf("    %s: \n        %s%s%s\n", path, RED_COLOR, error, NO_COLOR);
        ask_for_halt();
        return false;
    }

    uint *dfn = dlsym(lib, "DFLAG_N");
    *dfn = DFLAG_N;

    void *dfs = dlsym(lib, "DFLAGS");
    memcpy(dfs, DFLAGS, DFLAG_MAX * 32 * 2);

    ulong **link_id = dlsym(lib, "LAURE_LINK_ID");
    *link_id = LAURE_LINK_ID;

    void (*init_nb)() = dlsym(lib, "laure_init_name_buffs");
    init_nb();

    void (*set_transl)() = dlsym(lib, "laure_set_translators");
    set_transl();
    
    int (*perform_upload)(laure_session_t*) = dlsym(lib, "on_use");
    if (!perform_upload) {
        print_errhead("invalid shared cle; cannot load");
        printf("    function on_use(laure_session_t*) is undefined in CLE extension %s\n", path);
        ask_for_halt();
        return false;
    }
    int response = perform_upload(session);
    if (response != 0) {
        print_errhead("shared cle on_use returned non-zero code");
        printf("cle extension upload resulted with non-zero code (%d), failure", response);
        ask_for_halt();
        return false;
    }
    return true;
}

void strrev_via_swap(string s) {
    int l = strlen(s);

    for (int i = 0; i < l / 2; i++) {
        char b = s[i];
        s[i] = s[l-i-1];
        s[l-i-1] = b;
    }
}

Instance *get_nested_instance(Instance *atom, uint nesting, laure_scope_t *scope) {
    if (nesting == 0) return atom;

    assert(atom != NULL);
    if (!NESTED_DOC_AUTOGEN) {
        NESTED_DOC_AUTOGEN = strdup("this is autogenerated nested instance");
    }
    char buff[64];
    strcpy(buff, atom->name);
    for (int i = 0; i < nesting; i++) {
        strcat(buff, "[]");
    }
    Instance *gins = laure_scope_find_by_key(scope->glob, buff, true);

    if (! gins) {
        Instance *ins = instance_shallow_copy(atom);
        
        while (nesting) {
            void *img = laure_create_array_u(ins);
            ins = instance_new(MOCK_NAME, NESTED_DOC_AUTOGEN, img);
            ins->repr = array_repr;
            nesting--;
        }

        ins->name = strdup(buff);
        ins->repr = array_repr;

        // create constant nested type
        // which will be used later for hinting
        laure_scope_insert(scope->glob, ins);
        return ins;
    }
    return gins;
}

string get_nested_ins_name(Instance *atom, uint nesting, laure_scope_t *scope) {
    Instance *ins = get_nested_instance(atom, nesting, scope);
    if (! ins) return NULL;
    return ins->name;
}

void *laure_apply_pred(laure_expression_t *predicate_exp, laure_scope_t *scope) {
    laure_typeset *args_set = laure_typeset_new();

    uint all_count = laure_expression_get_count(predicate_exp->ba->set);
    if (predicate_exp->ba->has_resp) all_count--;

    uint idx = 0;
    uint *nestings = laure_alloc(sizeof(void*) * (all_count - predicate_exp->ba->body_len));

    for (int i = predicate_exp->ba->body_len; i < all_count; i++) {
        laure_expression_t *aexp = laure_expression_set_get_by_idx(predicate_exp->ba->set, i);
        uint nesting = aexp->flag;
        
        if (aexp->t == let_singlq) {
            if (! str_eq(aexp->s, GENERIC_T)) return NULL;
            // generic
            laure_typeset_push_decl(args_set, aexp->s);
        } else if (aexp->t == let_atom_sign) {
            // atomic type
            // (variable must be known)
            laure_typeset_push_decl(args_set, "@");
        } else if (aexp->t == let_auto) {
            printf("Auto cannot be used in arguments\n");
            return NULL;
        //} else if (str_eq(aexp->s, "_")) {
        //    laure_typeset_push_instance(args_set, NULL);
        } else {
            // datatype name
            string tname;
            if (nesting) {
                Instance *to_nest = laure_scope_find_by_key(scope, aexp->s, true);
                if (!to_nest) {
                    return NULL;
                }
                tname = get_nested_ins_name(to_nest, nesting, scope);
            } else {
                tname = aexp->s;
            }
            Instance *arg = laure_scope_find_by_key(scope, tname, true);
            if (! arg) {
                return NULL;
            }
            laure_typeset_push_instance(args_set, arg);
        }

        nestings[idx] = nesting;
        idx++;
    }

    laure_typedecl *resp;
    uint resp_nesting = 0;

    if (predicate_exp->ba->has_resp) {
        laure_expression_t *rexp = laure_expression_set_get_by_idx(predicate_exp->ba->set, idx);
        resp_nesting = rexp->flag;
        if (rexp->t == let_singlq) {
            resp = laure_typedecl_generic_create(rexp->s);
        } else if (rexp->t == let_auto) {
            resp = laure_typedecl_auto_create((laure_auto_type)rexp->flag);
        } else {
            string tname;
            if (resp_nesting) {
                Instance *to_nest = laure_scope_find_by_key(scope, rexp->s, true);
                if (!to_nest) {
                    return NULL;
                }
                tname = get_nested_ins_name(to_nest, resp_nesting, scope);
            } else tname = rexp->s;
            Instance *resp_ins = laure_scope_find_by_key(scope, tname, true);
            if (! resp_ins) {
                printf("%s is undefined; cannot consult.\n", tname);
                return NULL;
            }
            resp = laure_typedecl_instance_create(resp_ins);
        }
    } else {
        resp = NULL;
    }

    struct PredicateImage *img = predicate_header_new(predicate_exp->s, args_set, resp, predicate_exp->t == let_constraint);
    img->header.nestings = nestings;
    img->header.response_nesting = resp_nesting;
    return img;
}

apply_result_t laure_consult_predicate(
    laure_session_t *session, 
    laure_scope_t *scope, 
    laure_expression_t *predicate_exp, 
    string address
) {
    assert(predicate_exp->t == let_pred || predicate_exp->t == let_constraint);

    Instance *pred_ins;
    if (predicate_exp->s == NULL) pred_ins = HEAP_TABLE[predicate_exp->flag2];
    else pred_ins = laure_scope_find_by_key(scope, predicate_exp->s, true);

    bool is_template = PREDFLAG_IS_TEMPLATE(predicate_exp->flag);

    bool is_header = (pred_ins == NULL && predicate_exp->is_header);

    if (is_header) {
        if (is_header && predicate_exp->ba->body_len > 0) {
            return respond_apply(apply_error, "header should not contain body");
        }

        Instance *maybe_header = pred_ins;

        struct PredicateImage *img = laure_apply_pred(predicate_exp, scope);

        if (! img) return respond_apply(apply_error, "predicate hint is undefined");
        string name = strdup(predicate_exp->s);

        if (is_template) {
            // add variation with all anonymous instances
            uint l = img->header.args->length + (img->header.resp ? 1 : 0);
            laure_expression_set *set = NULL;
            for (uint i = 0; i < l; i++) set = laure_expression_set_link(
                set,
                laure_expression_create(let_var, NULL, false, "_", 0, NULL, "")
            );
            laure_expression_t *e = laure_expression_create(let_pred, NULL, false, name, true, laure_bodyargs_create(set, 0, img->header.resp != NULL), "");
            predicate_addvar(img, e);
        }

        Instance *ins = instance_new(name, LAURE_DOC_BUFF ? strdup(LAURE_DOC_BUFF) : NULL, img);
        ins->repr = predicate_exp->t == let_pred ? predicate_repr : constraint_repr;

        laure_scope_insert(scope, ins);
        _TEMP_PREDCONSULT_LAST = ins;

        LAURE_DOC_BUFF = NULL;
        return respond_apply(apply_ok, NULL);
    } else {
        if (is_template) {
            return respond_apply(apply_error, "template can only be header");
        }
        if (! pred_ins)
            return respond_apply(apply_error, "header for predicate is undefined");
        predicate_addvar(pred_ins->image, predicate_exp);
        return respond_apply(apply_ok, NULL);
    }
}

apply_result_t laure_apply(laure_session_t *session, string fact) {

    if (strlen(fact) > 1 && str_starts(fact, "//")) {
        // Saving comment
        fact = fact + 2;
        if (fact[0] == ' ') fact++;
        if (LAURE_DOC_BUFF == NULL) {
            LAURE_DOC_BUFF = strdup(fact);
        } else {
            char buffer[512];
            memset(buffer, 0, 512);
            int free_space = 512 - strlen(LAURE_DOC_BUFF) - strlen(fact) - 3;
            if (free_space < 0) {
                apply_result_t apply_res;
                apply_res.error = "doc buff overflow";
                apply_res.status = apply_error;
                return apply_res;
            }
            strcpy(buffer, LAURE_DOC_BUFF);
            strcat(buffer, "\n");
            strcat(buffer, fact);
            laure_free(LAURE_DOC_BUFF);
            LAURE_DOC_BUFF = strdup(buffer);
        }
        return respond_apply(apply_ok, NULL);
    }
    
    laure_parse_result result = laure_parse(fact);
    if (!result.is_ok) {
        return respond_apply(apply_error, result.err);
    }

    laure_expression_t *exp = result.exp;
    laure_expression_set *expset = laure_expression_compose_one(exp);
    
    qcontext qctx[1];
    *qctx = qcontext_temp(NULL, expset, NULL);

    control_ctx *cctx = control_new(session, session->scope, qctx, NULL, NULL, true);
    cctx->silent = true;

    qresp response = laure_start(cctx, expset);

    laure_scope_free(cctx->tmp_answer_scope);
    laure_free(cctx);
    
    if (response.state == q_error) {
        printf("\nError while applying statement:\n  %s%s%s\n    %s\n", RED_COLOR, exp->s, NO_COLOR, LAURE_ACTIVE_ERROR ? LAURE_ACTIVE_ERROR->msg : (string)response.payload);
        return respond_apply(apply_error, response.payload);
    }
    return respond_apply(apply_ok, NULL);
}

int laure_init_structures(laure_session_t *session) {
    return 1;
}

string consult_single(
    laure_session_t *session, 
    string fname, 
    FILE *file, 
    bool *failed,
    LAURE_FACT_REC(fact_receiver)
) {
    void **ifp = (void**)session->_included_filepaths;

    while (ifp[0]) {
        string fp = *ifp;
        if (str_eq(fp, fname)) {
            debug("%sFile%s %s%s was already consulted, skipping%s\n", RED_COLOR, BOLD_WHITE, fname, RED_COLOR, NO_COLOR);
            *failed = true;
            return NULL;
        }
        ifp++;
    }

    *ifp = strdup(fname);

    assert(fname && file);
    int ln = 0;

    struct stat *sb = laure_alloc(sizeof(struct stat));
    memset(sb, 0, sizeof(struct stat));

    if (fstat(fileno(file), sb) == 0 && S_ISDIR(sb->st_mode)) {

        laure_free(sb);

        string rev = laure_alloc(strlen(fname) + 1);
        memset(rev, 0, strlen(fname) + 1);
        strcpy(rev, fname);
        
        strrev_via_swap(rev);

        string spl = strtok(rev, "/");
        
        string s = laure_alloc(strlen(spl) + 1);
        memset(s, 0, strlen(spl) + 1);
        strcpy(s, spl);

        strrev_via_swap(s);

        string p = laure_alloc(strlen(fname) + strlen(s) + 6);
        memset(p, 0, strlen(fname) + strlen(s) + 6);
        strcpy(p, fname);
        strcat(p, "/");
        strcat(p, s);
        strcat(p, ".le");

        if (access(p, F_OK) != 0) {
            printf("%sUnable to find init file %s for package.\n", RED_COLOR, p);
            printf("Package %s is skipped.%s\n", fname, NO_COLOR);
            *failed = true;
            return NULL;
        }
        
        fclose(file);
        return p;
    }

    char line[512];
    memset(line, 0, 512);

    char buff[512];
    memset(buff, 0, 512);

    ssize_t read = 0;
    size_t len = 0;

    string readinto = laure_alloc(512);
    memset(readinto, 0, 512);

    while ((read = getline(&readinto, &len, file)) != -1) {
        strcpy(line, readinto);
        uint idx = 0;
        while (line[idx] == ' ') idx++;
        if (idx > 0 && str_starts(line + idx, "//")) continue;

        if (!strlen(line)) continue;
        if (lastc(line) == '\n')
            lastc(line) = 0;
        
        char last = lastc(line);

        if (buff[0]) {
            char newln[512];
            memset(newln, 0, 512);
            strcpy(newln, buff);
            strcat(newln, line);
            strcpy(line, newln);
            strcpy(buff, line);
        }

        if (str_starts(line, "//")) {
            fact_receiver(session, line);
            continue;
        }

        if (last != '}' && last != '.' && last != '%') {
            strcpy(buff, line);
        } else {
            if (lastc(line) == '.') lastc(line) = 0;
            if (lastc(line) == '%') lastc(line) = '\r';
            string line_ = strdup(line);
            string o = LAURE_CURRENT_ADDRESS;
            LAURE_CURRENT_ADDRESS = fname;
            apply_result_t result = fact_receiver(session, line_);
            if (result.status == apply_error) {
                *failed = 0;
                return 0;
                // printf("Error:\n  %s\n    %s%s%s\n", line_, RED_COLOR, result.error, NO_COLOR);
            }
            LAURE_CURRENT_ADDRESS = o;
            buff[0] = 0;
        }
    }

    laure_free(readinto);
    fclose(file);
    laure_init_structures(session);
    return NULL;
}

void laure_consult_recursive_with_receiver(laure_session_t *session, string path, int *failed, LAURE_FACT_REC(rec)) {
    string next = path;
    while (next) {
        next = consult_single(session, next, fopen(next, "r"), (bool*)failed, rec);
    }
}

void laure_consult_recursive(laure_session_t *session, string path, int *failed) {
    laure_consult_recursive_with_receiver(session, path, failed, laure_apply);
    laure_initialization(session->scope);
}

bool endswith(string s, string end) {
    char *c = s + strlen(s) - strlen(end);
    for (size_t i = 0; i < strlen(end); i++) {
        if (*c != end[i]) return false;
        c++;
    }
    return true;
}

string search_path(string original_path) {
    FILE *f = fopen(original_path, "r");
    if (f) {
        fclose(f);
        return strdup(original_path);
    } else if (! endswith(original_path, ".le")) {
        char buff[PATH_MAX];
        strcpy(buff, original_path);
        strcat(buff, ".le");
        f = fopen(buff, "r");
        if (f) {
            fclose(f);
            return strdup(buff);
        }
        return NULL;
    }
    return NULL;
}