
#include <hash_map.h>

static FILE *bindgen_file;
static NL_Strmap(int) already_defined;
static NL_Map(Cuik_Type*, Atom) typedefs;

static void bindgen_printf(char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    vprintf(fmt, args);
    vfprintf(bindgen_file, fmt, args);

    va_end(args);
}

static void bindgen_panic(char *msg) {
    puts(msg);
    exit(1);
}

static void print_odin_value(Cuik_Expr *expr, int depth, bool top) {
    switch (expr->exprs->op) {
        case EXPR_NONE:
            break;

        case EXPR_INT:
            bindgen_printf("%lld", expr->exprs->int_lit.lit);
            break;
        case EXPR_ENUM:
            // TODO: Expose the enum and then use the key/name here
            bindgen_printf("%d", expr->exprs->enum_val.num->value);
            break;
        case EXPR_FLOAT32:
        case EXPR_FLOAT64:
            bindgen_printf("%f", expr->exprs->float_lit);
            break;

        case EXPR_WCHAR:
            break;
        case EXPR_CHAR:
            bindgen_printf("%c", expr->exprs->char_lit);
            break;
        case EXPR_WSTR:
            break;
        case EXPR_STR:
            size_t len = expr->exprs->str.end - expr->exprs->str.start;
            char *terminated = (char *)cuik_malloc(len + 1);
            memcpy(terminated, expr->exprs->str.start, len); 
            terminated[len] = '\0';
            bindgen_printf("%s", terminated);
            cuik_free(terminated);
            break;

        case EXPR_BUILTIN_SYMBOL:
        case EXPR_UNKNOWN_SYMBOL:
        case EXPR_SYMBOL:
        case EXPR_CONSTRUCTOR:
        case EXPR_GENERIC: // C11's _Generic
        case EXPR_VA_ARG:

        case EXPR_INITIALIZER:

        case EXPR_CAST:
        case EXPR_PARAM: // special case of EXPR_VAR
        case EXPR_ASSIGN:
        case EXPR_PLUS_ASSIGN:
        case EXPR_MINUS_ASSIGN:
        case EXPR_TIMES_ASSIGN:
        case EXPR_SLASH_ASSIGN:
        case EXPR_PERCENT_ASSIGN:
        case EXPR_AND_ASSIGN:
        case EXPR_OR_ASSIGN:
        case EXPR_XOR_ASSIGN:
        case EXPR_SHL_ASSIGN:
        case EXPR_SHR_ASSIGN:

        case EXPR_PLUS:
        case EXPR_MINUS:
        case EXPR_TIMES:
        case EXPR_SLASH:
        case EXPR_PERCENT:
        case EXPR_AND:
        case EXPR_OR:
        case EXPR_XOR:
        case EXPR_SHL:
        case EXPR_SHR:

        case EXPR_CMPEQ:
        case EXPR_CMPNE:
        case EXPR_CMPGE:
        case EXPR_CMPLE:
        case EXPR_CMPGT:
        case EXPR_CMPLT:

        // tese are resolved by semantics pass
        case EXPR_PTRADD:
        case EXPR_PTRSUB:
        case EXPR_PTRDIFF:

        case EXPR_TERNARY:
        case EXPR_COMMA:

        case EXPR_LOGICAL_NOT:
        case EXPR_LOGICAL_AND:
        case EXPR_LOGICAL_OR:

        case EXPR_DEREF:
        case EXPR_ADDR:
        case EXPR_NEGATE:
        case EXPR_NOT:
        case EXPR_SUBSCRIPT:
        case EXPR_DOT:
        case EXPR_ARROW:
        case EXPR_DOT_R:
        case EXPR_ARROW_R:
        case EXPR_CALL:

        case EXPR_SWIZZLE: // GLSL stuff, .xyxy

        case EXPR_SIZEOF_T: // on type
        case EXPR_SIZEOF:   // on expr
        case EXPR_ALIGNOF_T:// on type

        case EXPR_PRE_INC:
        case EXPR_PRE_DEC:
        case EXPR_POST_INC:
        case EXPR_POST_DEC:
            break;
        default:
            bindgen_panic("Unhandled case in print_odin_value");break;
    }
}

static void print_odin_type(Cuik_Type* type, int depth, bool top) {
    if (type->kind == KIND_STRUCT || type->kind == KIND_UNION) {
        while (type != type->record.nominal) {
            type = type->record.nominal;
        }
    }

    if (!top) {
        ptrdiff_t search = nl_map_get(typedefs, type);
        if (search >= 0) {
            bindgen_printf("%s", typedefs[search].v);
            return;
        }
    }

    switch (type->kind) {
        case KIND_VOID: bindgen_printf("c.void"); break;
        case KIND_BOOL: bindgen_printf("c.bool"); break;
        case KIND_CHAR: bindgen_printf("c.char"); break;
        case KIND_SHORT: bindgen_printf("c.short"); break;
        case KIND_INT: bindgen_printf("c.int"); break;
        case KIND_LONG: bindgen_printf("c.long"); break;
        case KIND_LLONG: bindgen_printf("c.longlong"); break;
        case KIND_FLOAT: bindgen_printf("c.float"); break;
        case KIND_DOUBLE: bindgen_printf("c.double"); break;

        case KIND_ENUM: bindgen_printf("c.int"); break;

        case KIND_STRUCT:
        case KIND_UNION: {
            if (type->record.kid_count == 0) {
                bindgen_printf("struct {}");
                break;
            }

            bindgen_printf("struct %s{\n", type->kind == KIND_UNION ? "#raw_union " : "");

            for (size_t i = 0; i < type->record.kid_count; i++) {
                Member* m = &type->record.kids[i];

                // indentation
                for (size_t j = 0; j <= depth; j++) bindgen_printf("    ");

                if (m->name == NULL) {
                    bindgen_printf("using _: ");
                } else {
                    bindgen_printf("%s: ", m->name);
                }
                print_odin_type(cuik_canonical_type(m->type), depth + 1, false);
                bindgen_printf(",\n");
            }

            // indentation
            for (size_t j = 0; j < depth; j++) bindgen_printf("    ");
            bindgen_printf("}");
            break;
        }

        case KIND_ARRAY: {
            Cuik_Type* base = cuik_canonical_type(type->array.of);

            bindgen_printf("[%d]", type->array.count);
            print_odin_type(base, depth, false);
            break;
        }
        case KIND_PTR: {
            Cuik_Type* base = cuik_canonical_type(type->ptr_to);
            if (base->kind == KIND_CHAR) {
                bindgen_printf("cstring");
                break;
            } else if (base->kind == KIND_STRUCT || base->kind == KIND_UNION) {
                if (base->record.name) {
                    ptrdiff_t search = nl_map_get_cstr(already_defined, base->record.name);
                    if (search < 0) {
                        bindgen_printf("rawptr");
                        break;
                    }
                }
            }

            bindgen_printf("^");
            print_odin_type(base, depth, false);
            break;
        }

        case KIND_FUNC: {
            bindgen_printf("proc \"c\" (");
            for (size_t i = 0; i < type->func.param_count; i++) {
                if (i) bindgen_printf(", ");

                bindgen_printf("%s: ", type->func.param_list[i].name);
                print_odin_type(cuik_canonical_type(type->func.param_list[i].type), depth, false);
            }
            bindgen_printf(") -> ");
            print_odin_type(cuik_canonical_type(type->func.return_type), depth, false);
            break;
        }

        default: bindgen_panic("Unhandled case in print_odin_type"); break;
    }
}

static char *get_filename_from_path(char *path) {
    uint32_t path_len = strlen(path);
    for (int32_t i = path_len - 1; i >= 0; i -= 1) {
        if (path[i] == '/' || path[i] == '\\') {
            if (i < path_len - 1) {
                return &path[i + 1];
            }
        }
    }

    return path;
}

static bool is_in_sources(uint8_t *files_data, size_t num_files, const char* name) {
    size_t files_it = 0;
    for (size_t i = 0; i < num_files; ++i) {
        size_t len;
        memcpy(&len, files_data + files_it, sizeof(len));
        files_it += sizeof(len);

        char *filename = (char *)(files_data + files_it);
        files_it += len;

        if (strcmp(filename, name) == 0) {
            return true;
        }
    }

    return false;
}

static int get_files_in_directory(uint8_t **data, size_t *data_ptr, size_t *data_max, char *directory_path) {
    int rv = 0;

#ifdef _WIN32
    WIN32_FIND_DATA ffd;
    HANDLE h_find = INVALID_HANDLE_VALUE;

    uint32_t directory_path_len = strlen(directory_path);
    char *dir_path = (char *)cuik_malloc(directory_path_len + 3);
    memcpy(dir_path, directory_path, directory_path_len);
    dir_path[directory_path_len + 0] = '\\';
    dir_path[directory_path_len + 1] = '*';
    dir_path[directory_path_len + 2] = '\0';

    h_find = FindFirstFile(dir_path, &ffd);

    cuik_free(dir_path);

    if (INVALID_HANDLE_VALUE == h_find) 
    {
        return 0;
    } 

    do
    {
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
        }
        else
        {
            int slash_size = 1;
            size_t filename_size = strlen(ffd.cFileName) + 1;
            if (*data_ptr + sizeof(filename_size) + filename_size + directory_path_len + slash_size >= *data_max) {
                *data_max = *data_ptr + sizeof(filename_size) + filename_size + directory_path_len + slash_size;
                void *new_base = cuik_realloc(*data, *data_max);
                if (!new_base) {
                    return -1;
                }
                *data = new_base;
            }
            size_t *buf_size_t = (size_t *)(*data + *data_ptr);
            *buf_size_t = filename_size + directory_path_len + slash_size;
            *data_ptr += sizeof(filename_size);
            memcpy((*data + *data_ptr), directory_path, directory_path_len);
            (*data + *data_ptr)[directory_path_len] = '\\';
            *data_ptr += directory_path_len + slash_size;
            memcpy((*data + *data_ptr), ffd.cFileName, filename_size - 1);
            (*data + *data_ptr)[filename_size - 1] = '\0';
            *data_ptr += filename_size;
            rv += 1;
        }
    } while (FindNextFile(h_find, &ffd) != 0);
#else
#endif

    return rv;
}



int run_bindgen(int argc, const char** argv) {
    cuik_init(true);

    Cuik_DriverArgs args = {
        .version = CUIK_VERSION_C23,
        .target = cuik_target_host(),
        .toolchain = cuik_toolchain_host(),
    };
    cuik_parse_driver_args(&args, argc, argv);

    // we just want to type check
    args.syntax_only = true;
    args.preserve_ast = true;

    // compile source directories
    size_t dir_count = dyn_array_length(args.sources);
    if (dir_count == 0) {
        fprintf(stderr, "Expected directories as input\n");
        return 1;
    }

    size_t files_max = 16 * 1024;
    uint8_t *files_data = (uint8_t *)cuik_malloc(files_max);
    if (!files_data) {
        fprintf(stderr, "Unable to allocate memory for files data\n");
        return 1;
    }
    size_t files_it = 0;
    char *filename;

    for (size_t dir_it = 0; dir_it < dir_count; dir_it += 1) {
        int num_files = get_files_in_directory(&files_data, &files_it, &files_max, args.sources[dir_it]->data);
        if (num_files == 0) {
            continue;
        }
        if (num_files < 0) {
            return 1;
        }

        int relevant_files = 0;
        files_it = 0;
        Cuik_BuildStep** objs = cuik_malloc(num_files * sizeof(Cuik_BuildStep*));
        for (size_t i = 0; i < num_files; ++i) {
            size_t len;
            memcpy(&len, files_data + files_it, sizeof(len));
            files_it += sizeof(len);

            filename = (char *)(files_data + files_it);
            files_it += len;

            if (len > 3) {
                char ext = *(char*)(files_data + files_it - 2);
                char dot = *(char*)(files_data + files_it - 3);
                if (dot == '.' && (ext == 'h' || ext == 'c')) {
                    objs[relevant_files] = cuik_driver_cc(&args, filename);
                    relevant_files += 1;
                }
            }
        }

        // link (if no codegen is performed this doesn't *really* do much)
        Cuik_BuildStep* linked = cuik_driver_ld(&args, relevant_files, objs);
        if (!cuik_step_run(linked, NULL)) {
            fprintf(stderr, "damn...\n");
            return 1;
        }

        CompilationUnit* cu = cuik_driver_ld_get_cu(linked);

        char *package_name = get_filename_from_path(args.sources[dir_it]->data);
        filename = (char *)cuik_malloc(1024);
        snprintf(filename, 1024, "%s.odin", package_name);

        bindgen_file = fopen(filename, "wb");
        cuik_free(filename);
        if (!bindgen_file) {
            fprintf(stderr, "Unable to open 'bindgen_output.odin' for write\n");
            return 1;
        }

        bindgen_printf("package %s\n\nimport \"core:c\"\n\n", package_name);
        bindgen_printf("foreign import %s \"%s.lib\"\n", package_name, package_name);
        bindgen_printf("foreign %s {\n", package_name);
        CUIK_FOR_EACH_TU(tu, cu) {
            size_t stmt_count = cuik_num_of_top_level_stmts(tu);
            Stmt** stmts = cuik_get_top_level_stmts(tu);

            TokenStream* tokens = cuik_get_token_stream_from_tu(tu);

            // TODO: make these relative to the project path
            filename = (char *)cuikpp_get_files(tokens)[0].filename;
            filename = get_filename_from_path(filename);

            bindgen_printf("// TU %s\n", filename);

            for (size_t i = 0; i < stmt_count; i++) {
                ResolvedSourceLoc r = cuikpp_find_location(tokens, stmts[i]->loc.start);
                if (!is_in_sources(files_data, num_files, r.file->filename)) {
                    continue;
                }

                Atom name = stmts[i]->decl.name;
                Cuik_Type* type = cuik_canonical_type(stmts[i]->decl.type);

                if (stmts[i]->decl.attrs.is_typedef) {
                    ptrdiff_t search = nl_map_get(typedefs, type);
                    if (search < 0) {
                        // mark as defined
                        nl_map_put(typedefs, type, name);

                        if (type->kind == KIND_STRUCT || type->kind == KIND_UNION) {
                            // Assuming that this is a typedef and a struct definition
                            if (type->record.name) {
                                ptrdiff_t search = nl_map_get_cstr(already_defined, type->record.name);
                                if (search < 0) {
                                    nl_map_put_cstr(already_defined, type->record.name, 1);

                                    bindgen_printf("%s :: ", type->record.name);
                                    print_odin_type(type, 0, true);
                                    bindgen_printf("\n");

                                    bindgen_printf("%s :: %s\n", name, type->record.name);

                                }
                            } else {
                                bindgen_printf("%s :: ", name);
                                print_odin_type(type, 0, true);
                                bindgen_printf("\n");
                            }
                        } else {
                            bindgen_printf("%s :: ", name);
                            print_odin_type(type, 0, true);
                            bindgen_printf("\n");
                        }
                    }
                } else if (type->kind == KIND_FUNC) {
                    // normal function
                    ptrdiff_t search = nl_map_get_cstr(already_defined, name);
                    if (search < 0) {
                        nl_map_put_cstr(already_defined, name, 1);

                        if (!stmts[i]->decl.attrs.is_extern) {
                            bindgen_printf("%s :: ", name);
                            print_odin_type(type, 0, true);
                            bindgen_printf("\n");
                        }
                    }
                } else {
                    ptrdiff_t search = nl_map_get_cstr(already_defined, name);
                    if (search < 0) {
                        nl_map_put_cstr(already_defined, name, 1);

                        bindgen_printf("%s : ", name);
                        print_odin_type(type, 0, true);
                        bindgen_printf(" = ");
                        print_odin_value(stmts[i]->decl.initial, 0, true);
                        bindgen_printf("\n");
                    }
                }
            }

            nl_map_free(typedefs);
        }

        bindgen_printf("}\n");
        fclose(bindgen_file);
    }

    return 0;
}
