/*
 * See Licensing and Copyright notice in naev.h
 */

/**
 * @file font.c
 *
 * @brief OpenGL font rendering routines.
 *
 * Use a displaylist to store ASCII chars rendered with freefont
 * There are several drawing methods depending on whether you want
 * print it all, print to a max width, print centered or print a
 * block of text.
 *
 * There are hard-coded size limits.  256 characters for all routines
 * except gl_printText which has a 1024 limit.
 *
 * @todo check if length is too long
 */


#include "font.h"

#include "naev.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H

#include "log.h"
#include "array.h"
#include "conf.h"
#include "ndata.h"
#include "nfile.h"
#include "utf8.h"

#define HASH_LUT_SIZE 512 /**< Size of glyph look up table. */
#define MAX_ROWS 128 /**< Max number of rows per texture cache. */
#define DEFAULT_TEXTURE_SIZE 1024 /**< Default size of texture caches for glyphs. */


static gl_Matrix4 font_projection_mat;


/**
 * @brief Stores the row information for a font.
 */
typedef struct glFontRow_s {
   int x; /**< Current x offset. */
   int y; /**< Current y offset. */
   int h; /**< Height of the row. */
} glFontRow;


/**
 * @brief Stores a texture stash for fonts.
 */
typedef struct glFontTex_s {
   GLuint id; /**< Texture id. */
   glFontRow rows[ MAX_ROWS ]; /**< Stores font row information. */
} glFontTex;


/**
 * @brief Represents a character in the font.
 */
typedef struct glFontGlyph_s {
   uint32_t codepoint; /**< Real character. */
   GLfloat adv_x; /**< X advancement. */
   GLfloat adv_y; /**< Y advancement. */
   /* Offsets are stored in the VBO and thus not an issue. */
   glFontTex *tex; /**< Might be on different texture. */
   GLushort vbo_id; /**< VBO index to use. */
   int next; /**< Stored as a linked list. */
} glFontGlyph;


/**
 * @brief Stores a font character.
 */
typedef struct font_char_s {
   GLubyte *data; /**< Data of the character. */
   int w; /**< Width. */
   int h; /**< Height. */
   int off_x; /**< X offset when rendering. */
   int off_y; /**< Y offset when rendering. */
   GLfloat adv_x; /**< X advancement. */
   GLfloat adv_y; /**< Y advancement. */
   int tx; /**< Texture x position. */
   int ty; /**< Texture y position. */
   int tw; /**< Texture width. */
   int th; /**< Texture height. */
} font_char_t;


/**
 * @brief Freetype Font structure.
 */
typedef struct glFontStashFreetype_s {
   char *fontname; /**< Font name. */
   FT_Face face; /**< Face structure. */
   FT_Library library; /**< Library. */
   FT_Byte *fontdata; /**< Font data buffer. */
} glFontStashFreetype;


/**
 * @brief Font structure.
 */
typedef struct glFontStash_s {
   /* Core values (determine font). */
   char *fname; /**< Font list name. */
   unsigned int h; /**< Font height. */

   /* Generated values. */
   int tw; /**< Width of textures. */
   int th; /**< Height of textures. */
   glFontTex *tex; /**< Textures. */
   gl_vbo *vbo_tex; /**< VBO associated to texture coordinates. */
   gl_vbo *vbo_vert; /**< VBO associated to vertex coordinates. */
   GLfloat *vbo_tex_data; /**< Copy of texture coordinate data. */
   GLshort *vbo_vert_data; /**< Copy of vertex coordinate data. */
   int nvbo; /**< Amount of vbo data. */
   int mvbo; /**< Amount of vbo memory. */
   glFontGlyph *glyphs; /**< Unicode glyphs. */
   int lut[HASH_LUT_SIZE]; /**< Look up table. */

   /* Freetype stuff. */
   glFontStashFreetype *ft;

   int refcount; /**< Reference counting. */
} glFontStash;

/**
 * Available fonts stashes.
 */
static glFontStash *avail_fonts = NULL;  /**< These are pointed to by the font struct exposed in font.h. */

/* default font */
glFont gl_defFont; /**< Default font. */
glFont gl_smallFont; /**< Small font. */
glFont gl_defFontMono; /**< Default mono font. */


/* Last used colour. */
static const glColour *font_lastCol    = NULL; /**< Stores last colour used (activated by '\\a'). */
static int font_restoreLast      = 0; /**< Restore last colour. */


/*
 * prototypes
 */
static size_t font_limitSize( glFontStash *ft_font, int *width,
      const char *text, const int max );
static const glColour* gl_fontGetColour( uint32_t ch );
/* Get unicode glyphs from cache. */
static glFontGlyph* gl_fontGetGlyph( glFontStash *stsh, uint32_t ch );
/* Render.
 * TODO this should be changed to be more like font-stash (https://github.com/akrinke/Font-Stash)
 * In particular, instead of writing char by char, they should be batched up by textures and rendered
 * when gl_fontRenderEnd() is called, saving lots of opengl calls.
 */
static void gl_fontRenderStart( const glFontStash *stsh, double x, double y, const glColour *c );
static int gl_fontRenderGlyph( glFontStash *stsh, uint32_t ch, const glColour *c, int state, int forceColor );
static void gl_fontRenderEnd (void);

/*
 * Raw printing functions.
 */
static void gl_printOutline( const glFont *ft_font,
      const int width, const int height,
      double bx, double by, int line_height,
      const glColour* c, 
      const double outlineR,
      const char *text,
      int (*func)(const glFont*,
         const int, const int, const double, const double, int,
         const glColour*, const char*, const int )
      );
static int gl_printRawBase( const glFont *ft_font,
      const int unused1, const int unused2,
      const double x, const double y, int unused3,
      const glColour* c, const char *text, const int forceColor );
static int gl_printMaxRawBase( const glFont *ft_font,
      const int max, const int unused,
      const double x, const double y, int unused2,
      const glColour* c, const char *text, const int forceColor );
static int gl_printMidRawBase( const glFont *ft_font,
      const int width, const int unused,
      double x, const double y, int unused2,
      const glColour* c, const char *text, const int forceColor );
static int gl_printTextRawBase( const glFont *ft_font,
      const int width, const int height,
      double bx, double by, int line_height,
      const glColour* c, const char *text, const int forceColor );


/**
 * @brief Gets the font stash corresponding to a font.
 */
static glFontStash *gl_fontGetStash( const glFont *font )
{
   return &avail_fonts[ font->id ];
}


/**
 * @brief Adds a font glyph to the texture stash.
 */
static int gl_fontAddGlyphTex( glFontStash *stsh, font_char_t *ch, glFontGlyph *glyph )
{
   int i, j, n;
   glFontRow *r, *gr;
   glFontTex *tex;
   GLfloat *vbo_tex;
   GLshort *vbo_vert;
   GLfloat tx, ty, txw, tyh;
   GLfloat fw, fh;
   GLshort vx, vy, vw, vh;

   /* Find free row. */
   gr = NULL;
   for (i=0; i<array_size( stsh->tex ); i++) {
      for (j=0; j<MAX_ROWS; j++) {
         r = &stsh->tex->rows[j];
         /* Fits in current row, so use that. */
         if ((r->h == ch->h) && (r->x+ch->w < stsh->tw)) {
            tex = &stsh->tex[i];
            gr = r;
            break;
         }
         /* If not empty row, skip. */
         if (r->h != 0)
            continue;
         /* See if height fits. */
         if ((j==0) || (stsh->tex->rows[j-1].y+stsh->tex->rows[j-1].h+ch->h < stsh->th)) {
            r->h = ch->h;
            if (j>0)
               r->y = stsh->tex->rows[j-1].y + stsh->tex->rows[j-1].h;
            tex = &stsh->tex[i];
            gr = r;
            break;
         }
      }
   }

   /* Allocate new texture. */
   if (gr == NULL) {
      tex = &array_grow( &stsh->tex );
      memset( stsh->tex, 0, sizeof(glFontTex) );

      glGenTextures( 1, &tex->id );
      glBindTexture( GL_TEXTURE_2D, tex->id );

      /* Shouldn't ever scale - we'll generate appropriate size font. */
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

      /* Clamp texture .*/
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

      /* Initialize size. */
      glTexImage2D( GL_TEXTURE_2D, 0, GL_RED, stsh->tw, stsh->th, 0,
            GL_RED, GL_UNSIGNED_BYTE, NULL );

      /* Check for errors. */
      gl_checkErr();

      gr = &tex->rows[0];
      gr->h = ch->h;
   }

   /* Upload data. */
   glBindTexture( GL_TEXTURE_2D, tex->id );
   glPixelStorei(GL_UNPACK_ALIGNMENT,1);
   glTexSubImage2D( GL_TEXTURE_2D, 0, gr->x, gr->y, ch->w, ch->h,
         GL_RED, GL_UNSIGNED_BYTE, ch->data );

   /* Check for error. */
   gl_checkErr();

   /* Update VBOs. */
   stsh->nvbo++;
   if (stsh->nvbo > stsh->mvbo) {
      stsh->mvbo *= 2;
      stsh->vbo_tex_data  = realloc( stsh->vbo_tex_data,  8*stsh->mvbo*sizeof(GLfloat) );
      stsh->vbo_vert_data = realloc( stsh->vbo_vert_data, 8*stsh->mvbo*sizeof(GLshort) );
   }
   n = 8*stsh->nvbo;
   vbo_tex  = &stsh->vbo_tex_data[n-8];
   vbo_vert = &stsh->vbo_vert_data[n-8];
   /* We do something like the following for vertex coordinates.
      *
      *
      *  +----------------- top reference   \  <------- font->h
      *  |                                  |
      *  |                                  | --- off_y
      *  +----------------- glyph top       /
      *  |
      *  |
      *  +----------------- glyph bottom
      *  |
      *  v   y
      *
      *
      *  +----+------------->  x
      *  |    |
      *  |    glyph start
      *  |
      *  side reference
      *
      *  \----/
      *   off_x
      */
   /* Temporary variables. */
   fw  = (GLfloat) stsh->tw;
   fh  = (GLfloat) stsh->th;
   tx  = (GLfloat) gr->x / fw;
   ty  = (GLfloat) gr->y / fh;
   txw = (GLfloat) (gr->x + ch->w) / fw;
   tyh = (GLfloat) (gr->y + ch->h) / fh;
   vx  = ch->off_x;
   vy  = ch->off_y - ch->h;
   vw  = ch->w;
   vh  = ch->h;
   /* Texture coords. */
   vbo_tex[ 0 ] = tx;  /* Top left. */
   vbo_tex[ 1 ] = ty;
   vbo_tex[ 2 ] = txw; /* Top right. */
   vbo_tex[ 3 ] = ty;
   vbo_tex[ 4 ] = tx;  /* Bottom left. */
   vbo_tex[ 5 ] = tyh;
   vbo_tex[ 6 ] = txw; /* Bottom right. */
   vbo_tex[ 7 ] = tyh;
   /* Vertex coords. */
   vbo_vert[ 0 ] = vx;    /* Top left. */
   vbo_vert[ 1 ] = vy+vh;
   vbo_vert[ 2 ] = vx+vw; /* Top right. */
   vbo_vert[ 3 ] = vy+vh;
   vbo_vert[ 4 ] = vx;    /* Bottom left. */
   vbo_vert[ 5 ] = vy;
   vbo_vert[ 6 ] = vx+vw; /* Bottom right. */
   vbo_vert[ 7 ] = vy;
   /* Update vbos. */
   gl_vboData( stsh->vbo_tex,  sizeof(GLfloat)*n,  stsh->vbo_tex_data );
   gl_vboData( stsh->vbo_vert, sizeof(GLshort)*n, stsh->vbo_vert_data );

   /* Add space for the new character. */
   gr->x += ch->w;

   /* Save glyph data. */
   glyph->vbo_id = (n-8)/2;
   glyph->tex = tex;

   /* Since the VBOs have possibly changed, we have to reset the data. */
   gl_vboActivateAttribOffset( stsh->vbo_vert, shaders.font.vertex,
         0, 2, GL_SHORT, 0 );
   gl_vboActivateAttribOffset( stsh->vbo_tex, shaders.font.tex_coord,
         0, 2, GL_FLOAT, 0 );

   return 0;
}


/**
 * @brief Clears the restoration.
 */
void gl_printRestoreClear (void)
{
   font_lastCol = NULL;
}


/**
 * @brief Restores last colour.
 */
void gl_printRestoreLast (void)
{
   if (font_lastCol != NULL)
      font_restoreLast = 1;
}


/**
 * @brief Initializes a restore structure.
 *    @param restore Structure to initialize.
 */
void gl_printRestoreInit( glFontRestore *restore )
{
   memset( restore, 0, sizeof(glFontRestore) );
}


/**
 * @brief Restores last colour from a restore structure.
 *    @param restore Structure to restore.
 */
void gl_printRestore( const glFontRestore *restore )
{
   if (restore->col != NULL) {
      font_lastCol = restore->col;
      font_restoreLast = 1;
   }
}


/**
 * @brief Stores the colour information from a piece of text limited to max characters.
 *    @param restore Structure to save colour information to.
 *    @param text Text to extract colour information from.
 *    @param max Maximum number of characters to process.
 */
void gl_printStoreMax( glFontRestore *restore, const char *text, int max )
{
   int i;
   const glColour *col;

   col = restore->col; /* Use whatever is there. */
   for (i=0; (text[i]!='\0') && (i<=max); i++) {
      /* Only want escape sequences. */
      if (text[i] != '\a')
         continue;

      /* Get colour. */
      if ((i+1<=max) && (text[i+1]!='\0')) {
         col = gl_fontGetColour( text[i+1] );
         i += 1;
      }
   }

   restore->col = col;
}


/**
 * @brief Stores the colour information from a piece of text.
 *    @param restore Structure to save colour information to.
 *    @param text Text to extract colour information from.
 */
void gl_printStore( glFontRestore *restore, const char *text )
{
   gl_printStoreMax( restore, text, INT_MAX );
}


/**
 * @brief Limits the text to max.
 *
 *    @param ft_font Font to calculate width with.
 *    @param width Actual width it takes up.
 *    @param text Text to parse.
 *    @param max Max to look for.
 *    @return Number of characters that fit.
 */
static size_t font_limitSize( glFontStash *ft_font, int *width,
      const char *text, const int max )
{
   int n;
   size_t i;
   uint32_t ch;
   int adv_x;

   /* Avoid segfaults. */
   if ((text == NULL) || (text[0]=='\0'))
      return 0;

   /* limit size */
   i = 0;
   n = 0;
   while ((ch = u8_nextchar( text, &i ))) {
      /* Ignore escape sequence. */
      if (ch == '\a') {
         if (text[i] != '\0')
            i++;
         continue;
      }

      /* Count length. */
      glFontGlyph *glyph = gl_fontGetGlyph( ft_font, ch );
      adv_x = glyph->adv_x;

      /* See if enough room. */
      n += adv_x;
      if (n > max) {
         u8_dec( text, &i );
         n -= adv_x; /* actual size */
         break;
      }
   }

   if (width != NULL)
      (*width) = n;
   return i;
}


/**
 * @brief Gets the number of characters in text that fit into width.
 *
 *    @param ft_font Font to use.
 *    @param text Text to check.
 *    @param width Width to match.
 *    @return Number of characters that fit.
 */
int gl_printWidthForTextLine( const glFont *ft_font, const char *text, int width )
{
   glFontStash *stsh = gl_fontGetStash( ft_font );
   return font_limitSize( stsh, NULL, text, width );
}


/**
 * @brief Gets the number of characters in text that fit into width,
 *        assuming your intent is to word-wrap at said width.
 *
 *    @param ft_font Font to use.
 *    @param text Text to check.
 *    @param width Width to match.
 *    @return Number of characters that fit.
 */
int gl_printWidthForText( const glFont *ft_font, const char *text,
      const int width )
{
   int lastspace;
   size_t i;
   uint32_t ch;
   GLfloat n;

   if (ft_font == NULL)
      ft_font = &gl_defFont;
   glFontStash *stsh = gl_fontGetStash( ft_font );

   /* limit size per line */
   lastspace = 0; /* last ' ' or '\n' in the text */
   n = 0.; /* current width */
   i = 0; /* current position */
   while ((text[i] != '\n') && (text[i] != '\0')) {
      /* Characters we should ignore. */
      if (text[i] == '\t') {
         i++;
         continue;
      }

      /* Ignore escape sequence. */
      if (text[i] == '\a') {
         if (text[i+1] != '\0')
            i += 2;
         else
            i += 1;
         continue;
      }

      /* Save last space. */
      if (text[i] == ' ')
         lastspace = i;

      ch = u8_nextchar( text, &i );
      /* Unicode. */
      glFontGlyph *glyph = gl_fontGetGlyph( stsh, ch );
      if (glyph!=NULL)
         n += glyph->adv_x;

      /* Check if out of bounds. */
      if (n > (GLfloat)width) {
         if (lastspace > 0)
            return lastspace;
         else {
            u8_dec( text, &i );
            return i;
         }
      }
   }

   return i;
}

/**
 * @brief Shows a halo for increased contrast around the text.
 */
static void gl_printOutline( const glFont *ft_font,
      const int width, const int height,
      double bx, double by, int line_height,
      const glColour* c, 
      const double outlineR,
      const char *text,
      int (*func)(const glFont*,
         const int, const int, const double, const double, int,
         const glColour*, const char*, const int )
      )
{
   const glColour *bg;
   double radius;

   /* TODO: This method works, but is inefficient. Should probably be
    * ultimately replaced with use of signed distance fields or some
    * other more efficient method (signed distance fields seem to be
    * the "right" solution). */
   if ((c == NULL) || (c->a >= 1.)) {
      if(outlineR != -1){
         radius = outlineR;
      } else {
         radius = 1.;
      }
      bg = &cGrey10;

      func( ft_font, width, height, bx - radius, by, line_height, bg, text, 1 );
      func( ft_font, width, height, bx + radius, by, line_height, bg, text, 1 );
      func( ft_font, width, height, bx, by - radius, line_height, bg, text, 1 );
      func( ft_font, width, height, bx, by + radius, line_height, bg, text, 1 );
      func( ft_font, width, height, bx - radius, by - radius, line_height, bg, text, 1 );
      func( ft_font, width, height, bx + radius, by + radius, line_height, bg, text, 1 );
      func( ft_font, width, height, bx + radius, by - radius, line_height, bg, text, 1 );
      func( ft_font, width, height, bx - radius, by + radius, line_height, bg, text, 1 );

   }
}


/**
 * @brief Wrapped by gl_printRaw.
 *
 *    @param ft_font Font to use
 *    @param unused1 Unused.
 *    @param unused2 Unused.
 *    @param x X position to put text at.
 *    @param y Y position to put text at.
 *    @param unused3 Unused.
 *    @param c Colour to use (uses white if NULL)
 *    @param text String to display.
 *    @param forceColor Whether or not to force the color.
 */
static int gl_printRawBase( const glFont *ft_font,
      const int unused1, const int unused2,
      const double x, const double y, int unused3,
      const glColour* c, const char *text, const int forceColor )
{
   (void) unused1;
   (void) unused2;
   (void) unused3;
   int s;
   size_t i;
   uint32_t ch;

   if (ft_font == NULL)
      ft_font = &gl_defFont;
   glFontStash *stsh = gl_fontGetStash( ft_font );

   /* Render it. */
   s = 0;
   i = 0;
   gl_fontRenderStart(stsh, x, y, c);
   while ((ch = u8_nextchar( text, &i )))
      s = gl_fontRenderGlyph( stsh, ch, c, s, forceColor );
   gl_fontRenderEnd();
   return 0;
}

/** 
 * @brief Wrapper for gl_printRaw for map overlay markers
 *
 * See gl_printRaw params (minus outlineR)
 */
void gl_printMarkerRaw( const glFont *ft_font,
      const double x, const double y,
      const glColour* c, const char *text)
{
   gl_printOutline( ft_font, 0, 0, x, y, 0, c, 1, text, gl_printRawBase);
   gl_printRawBase( ft_font, 0, 0, x, y, 0, c, text, 0 );
}



/**
 * @brief Prints text on screen.
 *
 * Defaults ft_font to gl_defFont if NULL.
 *
 *    @param ft_font Font to use
 *    @param x X position to put text at.
 *    @param y Y position to put text at.
 *    @param c Colour to use (uses white if NULL)
 *    @param outlineR Radius in px of outline (-1 for default, 0 for none)
 *    @param text String to display.
 */
void gl_printRaw( const glFont *ft_font,
      const double x, const double y,
      const glColour* c, const double outlineR, const char *text)
{
   if (outlineR > 0.)
      gl_printOutline( ft_font, 0, 0, x, y, 0, c, outlineR, text, gl_printRawBase);
   gl_printRawBase( ft_font, 0, 0, x, y, 0, c, text, 0 );
}


/**
 * @brief Prints text on screen like printf.
 *
 * Defaults ft_font to gl_defFont if NULL.
 *
 *    @param ft_font Font to use (NULL means gl_defFont)
 *    @param x X position to put text at.
 *    @param y Y position to put text at.
 *    @param c Colour to use (uses white if NULL)
 *    @param fmt String formatted like printf to print.
 */
void gl_print( const glFont *ft_font,
      const double x, const double y,
      const glColour* c, const char *fmt, ... )
{
   /*float h = ft_font->h / .63;*/ /* slightly increase fontsize */
   char text[256]; /* holds the string */
   va_list ap;

   if (fmt == NULL) return;
   else { /* convert the symbols to text */
      va_start(ap, fmt);
      vsnprintf(text, 256, fmt, ap);
      va_end(ap);
   }

   gl_printRaw( ft_font, x, y, c, -1., text );
}


/**
 * @brief Wrapped by gl_printMaxRaw.
 *
 *    @param ft_font Font to use.
 *    @param max Maximum length to reach.
 *    @param unused Unused.
 *    @param x X position to display text at.
 *    @param y Y position to display text at.
 *    @param unused2 Unused.
 *    @param c Colour to use (NULL defaults to white).
 *    @param text String to display.
 *    @param forceColor Whether or not to force the color.
 *    @return The number of characters it had to suppress.
 */
static int gl_printMaxRawBase( const glFont *ft_font,
      const int max, const int unused,
      const double x, const double y, int unused2,
      const glColour* c, const char *text, const int forceColor )
{
   (void) unused;
   (void) unused2;
   int s;
   size_t ret, i;
   uint32_t ch;

   if (ft_font == NULL)
      ft_font = &gl_defFont;
   glFontStash *stsh = gl_fontGetStash( ft_font );

   /* Limit size. */
   ret = font_limitSize( stsh, NULL, text, max );

   /* Render it. */
   s = 0;
   gl_fontRenderStart(stsh, x, y, c);
   i = 0;
   while ((ch = u8_nextchar( text, &i )) && (i <= ret))
      s = gl_fontRenderGlyph( stsh, ch, c, s, forceColor );
   gl_fontRenderEnd();

   return ret;
}


/**
 * @brief Behaves like gl_printRaw but stops displaying text after a certain distance.
 *
 *    @param ft_font Font to use.
 *    @param max Maximum length to reach.
 *    @param x X position to display text at.
 *    @param y Y position to display text at.
 *    @param c Colour to use (NULL defaults to white).
 *    @param outlineR Radius in px of outline (-1 for default, 0 for none)
 *    @param text String to display.
 *    @return The number of characters it had to suppress.
 */
int gl_printMaxRaw( const glFont *ft_font, const int max,
      const double x, const double y,
      const glColour* c, const double outlineR, const char *text)
{
   if (outlineR > 0.)
      gl_printOutline( ft_font, max, 0, x, y, 0, c, outlineR, text, gl_printMaxRawBase );
   return gl_printMaxRawBase( ft_font, max, 0, x, y, 0, c, text, 0 );
}


/**
 * @brief Behaves like gl_print but stops displaying text after reaching a certain length.
 *
 *    @param ft_font Font to use (NULL means use gl_defFont).
 *    @param max Maximum length to reach.
 *    @param x X position to display text at.
 *    @param y Y position to display text at.
 *    @param c Colour to use (NULL defaults to white).
 *    @param fmt String to display formatted like printf.
 *    @return The number of characters it had to suppress.
 */
int gl_printMax( const glFont *ft_font, const int max,
      const double x, const double y,
      const glColour* c, const char *fmt, ... )
{
   /*float h = ft_font->h / .63;*/ /* slightly increase fontsize */
   char text[256]; /* holds the string */
   va_list ap;

   if (fmt == NULL) return -1;
   else { /* convert the symbols to text */
      va_start(ap, fmt);
      vsnprintf(text, 256, fmt, ap);
      va_end(ap);
   }

   return gl_printMaxRaw( ft_font, max, x, y, c, -1., text );
}


/**
 * @brief Wrapped by gl_printMidRaw.
 *
 *    @param ft_font Font to use.
 *    @param width Width of area to center in.
 *    @param unused Unused.
 *    @param x X position to display text at.
 *    @param y Y position to display text at.
 *    @param unused2 Unused.
 *    @param c Colour to use for text (NULL defaults to white).
 *    @param text String to display.
 *    @param forceColor Whether or not to force the color.
 *    @return The number of characters it had to truncate.
 */
static int gl_printMidRawBase( const glFont *ft_font,
      const int width, const int unused,
      double x, const double y, int unused2,
      const glColour* c, const char *text, const int forceColor )
{
   (void) unused;
   (void) unused2;
   /*float h = ft_font->h / .63;*/ /* slightly increase fontsize */
   int n, s;
   size_t ret, i;
   uint32_t ch;

   if (ft_font == NULL)
      ft_font = &gl_defFont;
   glFontStash *stsh = gl_fontGetStash( ft_font );

   /* limit size */
   n = 0;
   ret = font_limitSize( stsh, &n, text, width );
   x += (double)(width - n)/2.;

   /* Render it. */
   s = 0;
   gl_fontRenderStart(stsh, x, y, c);
   i = 0;
   while ((ch = u8_nextchar( text, &i )) && (i <= ret))
      s = gl_fontRenderGlyph( stsh, ch, c, s, forceColor );
   gl_fontRenderEnd();

   return ret;
}


/**
 * @brief Displays text centered in position and width.
 *
 * Will truncate if text is too long.
 *
 *    @param ft_font Font to use.
 *    @param width Width of area to center in.
 *    @param x X position to display text at.
 *    @param y Y position to display text at.
 *    @param c Colour to use for text (NULL defaults to white).
 *    @param outlineR Radius in px of outline (-1 for default, 0 for none)
 *    @param text String to display.
 *    @return The number of characters it had to truncate.
 */
int gl_printMidRaw( 
      const glFont *ft_font, 
      const int width,
      double x, 
      const double y,
      const glColour* c, 
      const double outlineR,
      const char *text
      )
{
   if (outlineR > 0.)
      gl_printOutline( ft_font, width, 0, x, y, 0, c, outlineR, text, gl_printMidRawBase);
   return gl_printMidRawBase( ft_font, width, 0, x, y, 0, c, text, 0 );
}


/**
 * @brief Displays text centered in position and width.
 *
 * Will truncate if text is too long.
 *
 *    @param ft_font Font to use (NULL defaults to gl_defFont)
 *    @param width Width of area to center in.
 *    @param x X position to display text at.
 *    @param y Y position to display text at.
 *    @param c Colour to use for text (NULL defaults to white).
 *    @param fmt Text to display formatted like printf.
 *    @return The number of characters it had to truncate.
 */
int gl_printMid( const glFont *ft_font, const int width,
      double x, const double y,
      const glColour* c, const char *fmt, ... )
{
   /*float h = ft_font->h / .63;*/ /* slightly increase fontsize */
   char text[256]; /* holds the string */
   va_list ap;

   if (fmt == NULL) return -1;
   else { /* convert the symbols to text */
      va_start(ap, fmt);
      vsnprintf(text, 256, fmt, ap);
      va_end(ap);
   }

   return gl_printMidRaw( ft_font, width, x, y, c, -1., text );
}


/**
 * @brief Wrapped by gl_printTextRaw.
 *
 *    @param ft_font Font to use.
 *    @param width Maximum width to print to.
 *    @param height Maximum height to print to.
 *    @param bx X position to display text at.
 *    @param by Y position to display text at.
 *    @param line_height Height to use for each line (or 0 for default)
 *    @param c Colour to use (NULL defaults to white).
 *    @param text String to display.
 *    @param forceColor Whether or not to force the color.
 *    @return 0 on success.
 */
static int gl_printTextRawBase( const glFont *ft_font,
      const int width, const int height,
      double bx, double by, int line_height,
      const glColour* c, const char *text, const int forceColor )
{
   int p, s;
   double x,y;
   size_t i, ret;
   uint32_t ch;

   if (ft_font == NULL)
      ft_font = &gl_defFont;
   glFontStash *stsh = gl_fontGetStash( ft_font );

   x = bx;
   y = by + height - (double)stsh->h; /* y is top left corner */

   /* Defalut to 1.5 line height. */
   if (line_height == 0)
      line_height = 1.5*(double)stsh->h;

   /* Clears restoration. */
   gl_printRestoreClear();

   ch = text[0]; /* In case of a 0-width first line (ret==p) below, we just care if text is empty or not. */
   i = 0;
   s = 0;
   p = 0; /* where we last drew up to */
   while (y - by > -1e-5) {
      ret = p + gl_printWidthForText( ft_font, &text[p], width );

      /* Must restore stuff. */
      gl_printRestoreLast();

      /* Render it. */
      gl_fontRenderStart(stsh, x, y, c);
      for (i=p; i<ret; ) {
         ch = u8_nextchar( text, &i);
         s = gl_fontRenderGlyph( stsh, ch, c, s, forceColor );
      }
      gl_fontRenderEnd();

      if (ch == '\0')
         break;
      p = i;
      if ((text[p] == '\n') || (text[p] == ' '))
         p++; /* Skip "empty char". */
      y -= line_height; /* move position down */
   }

   return 0;
}


/**
 * @brief Prints a block of text that fits in the dimensions given.
 *
 * Positions are based on origin being top-left.
 *
 *    @param ft_font Font to use.
 *    @param width Maximum width to print to.
 *    @param height Maximum height to print to.
 *    @param bx X position to display text at.
 *    @param by Y position to display text at.
 *    @param line_height Height of each line to print.
 *    @param c Colour to use (NULL defaults to white).
 *    @param outlineR Radius in px of outline (-1 for default, 0 for none)
 *    @param text String to display.
 *    @return 0 on success.
 * prints text with line breaks included to a maximum width and height preset
 */
int gl_printTextRaw( const glFont *ft_font,
      const int width, const int height,
      double bx, double by, int line_height,
      const glColour* c, 
      const double outlineR,
      const char *text
    )
{
   if (outlineR > 0.)
      gl_printOutline( ft_font, width, height, bx, by, line_height, c, outlineR, text, gl_printTextRawBase );
   return gl_printTextRawBase( ft_font, width, height, bx, by, line_height, c, text, 0 );
}


/**
 * @brief Prints a block of text that fits in the dimensions given.
 *
 * Positions are based on origin being top-left.
 *
 *    @param ft_font Font to use (NULL defaults to gl_defFont).
 *    @param width Maximum width to print to.
 *    @param height Maximum height to print to.
 *    @param bx X position to display text at.
 *    @param by Y position to display text at.
 *    @param line_height Height of each line to print.
 *    @param c Colour to use (NULL defaults to white).
 *    @param fmt Text to display formatted like printf.
 *    @return 0 on success.
 * prints text with line breaks included to a maximum width and height preset
 */
int gl_printText( const glFont *ft_font,
      const int width, const int height,
      double bx, double by, int line_height,
      const glColour* c, const char *fmt, ... )
{
   /*float h = ft_font->h / .63;*/ /* slightly increase fontsize */
   char text[4096]; /* holds the string */
   va_list ap;

   if (fmt == NULL) return -1;
   else { /* convert the symbols to text */
      va_start(ap, fmt);
      vsnprintf(text, 4096, fmt, ap);
      va_end(ap);
   }

   return gl_printTextRaw( ft_font, width, height, bx, by, line_height, c, -1., text );
}


/**
 * @brief Gets the width that it would take to print some text.
 *
 * Does not display text on screen.
 *
 *    @param ft_font Font to use (NULL defaults to gl_defFont).
 *    @param text Text to calculate the length of.
 *    @return The length of the text in pixels.
 */
int gl_printWidthRaw( const glFont *ft_font, const char *text )
{
   GLfloat n;
   size_t i;
   uint32_t ch;

   if (ft_font == NULL)
      ft_font = &gl_defFont;
   glFontStash *stsh = gl_fontGetStash( ft_font );

   n = 0.;
   i = 0;
   while ((ch = u8_nextchar( text, &i ))) {
      /* Ignore escape sequence. */
      if (ch == '\a') {
         if (text[i] != '\0')
            i++;

         continue;
      }

      /* Increment width. */
      glFontGlyph *glyph = gl_fontGetGlyph( stsh, ch );
      n += glyph->adv_x;
   }

   return (int)n;
}


/**
 * @brief Gets the width that it would take to print some text.
 *
 * Does not display text on screen.
 *
 *    @param ft_font Font to use (NULL defaults to gl_defFont).
 *    @param fmt Text to calculate the length of.
 *    @return The length of the text in pixels.
 */
int gl_printWidth( const glFont *ft_font, const char *fmt, ... )
{
   char text[256]; /* holds the string */
   va_list ap;

   if (fmt == NULL) return 0;
   else { /* convert the symbols to text */
      va_start(ap, fmt);
      vsnprintf(text, 256, fmt, ap);
      va_end(ap);
   }

   return gl_printWidthRaw( ft_font, text );
}


/**
 * @brief Gets the height of a non-formatted string.
 *
 * Does not display the text on screen.
 *
 *    @param ft_font Font to use (NULL defaults to gl_defFont).
 *    @param width Width to jump to next line once reached.
 *    @param text Text to get the height of.
 *    @return The height of the text.
 */
int gl_printHeightRaw( const glFont *ft_font,
      const int width, const char *text )
{
   int i, p;
   double y;

   if (ft_font == NULL)
      ft_font = &gl_defFont;

   /* Check 0 length strings. */
   if (text[0] == '\0')
      return 0;

   y = 0.;
   p = 0;
   do {
      i = gl_printWidthForText( ft_font, &text[p], width );
      p += i + 1;
      y += 1.5*(double)ft_font->h; /* move position down */
   } while (text[p-1] != '\0');

   return (int) (y - 0.5*(double)ft_font->h) + 1;
}

/**
 * @brief Gets the height of the text if it were printed.
 *
 * Does not display the text on screen.
 *
 *    @param ft_font Font to use (NULL defaults to gl_defFont).
 *    @param width Width to jump to next line once reached.
 *    @param fmt Text to get the height of in printf format.
 *    @return The height of the text.
 */
int gl_printHeight( const glFont *ft_font,
      const int width, const char *fmt, ... )
{
   char text[1024]; /* holds the string */
   va_list ap;

   if (fmt == NULL) return -1;
   else { /* convert the symbols to text */
      va_start(ap, fmt);
      vsnprintf(text, 1024, fmt, ap);
      va_end(ap);
   }

   return gl_printHeightRaw( ft_font, width, text );
}


/*
 *
 * G L _ F O N T
 *
 */
/**
 */
static int font_makeChar( glFontStash *stsh, font_char_t *c, uint32_t ch )
{
   FT_Bitmap bitmap;
   FT_GlyphSlot slot;
   FT_UInt glyph_index;
   int i, w,h, len;
   glFontStashFreetype *ft;

   /* Empty font. */
   if (stsh->ft==NULL) {
      WARN(_("Font has no freetype information!"));
      return -1;
   }

   len = array_size(stsh->ft);
   for (i=0; i<len; i++) {
      ft = &stsh->ft[i];

      /* Get glyph index. */
      glyph_index = FT_Get_Char_Index( ft->face, ch );
      /* Skip missing unless last font. */
      if (glyph_index==0) {
         if (i<len-1)
            continue;
         else {
            WARN(_("Unicode character '%#x' not found in font! Using missing glyph."), ch);
            ft = &stsh->ft[0]; /* Fallback to first font. */
         }
      }

      /* Load the glyph. */
      if (FT_Load_Glyph( ft->face, glyph_index, FT_LOAD_RENDER | FT_LOAD_NO_BITMAP | FT_LOAD_TARGET_NORMAL)) {
         WARN(_("FT_Load_Glyph failed."));
         return -1;
      }

      slot = ft->face->glyph; /* Small shortcut. */
      bitmap = slot->bitmap; /* to simplify */
      if (bitmap.pixel_mode != FT_PIXEL_MODE_GRAY)
         WARN(_("Font '%s' not using FT_PIXEL_MODE_GRAY!"), ft->fontname);

      /* need the POT wrapping for opengl */
      w = bitmap.width;
      h = bitmap.rows;

      /* Store data. */
      c->data = malloc( sizeof(GLubyte) * w*h );
      if (bitmap.buffer == NULL)
         memset( c->data, 0, sizeof(GLubyte) * w*h );
      else
         memcpy( c->data, bitmap.buffer, sizeof(GLubyte) * w*h );
      c->w     = w;
      c->h     = h;
      c->off_x = slot->bitmap_left;
      c->off_y = slot->bitmap_top;
      c->adv_x = (GLfloat)slot->advance.x / 64.;
      c->adv_y = (GLfloat)slot->advance.y / 64.;

      return 0;
   }
   WARN(_("Unable to load character '%#x'!"), ch);
   return -1;
}


/**
 * @brief Starts the rendering engine.
 */
static void gl_fontRenderStart( const glFontStash* font, double x, double y, const glColour *c )
{
   double a;

   glUseProgram(shaders.font.program);

   font_projection_mat = gl_Matrix4_Translate(gl_view_matrix, round(x), round(y), 0);

   /* Handle colour. */
   if (font_restoreLast) {
      a   = (c==NULL) ? 1. : c->a;
      gl_uniformAColor(shaders.font.color, font_lastCol, a);
   }
   else {
      if (c==NULL)
         gl_uniformColor(shaders.font.color, &cWhite);
      else
         gl_uniformColor(shaders.font.color, c);
   }
   font_restoreLast = 0;

   /* Activate the appropriate VBOs. */
   glEnableVertexAttribArray( shaders.font.vertex );
   gl_vboActivateAttribOffset( font->vbo_vert, shaders.font.vertex,
         0, 2, GL_SHORT, 0 );
   glEnableVertexAttribArray( shaders.font.tex_coord );
   gl_vboActivateAttribOffset( font->vbo_tex, shaders.font.tex_coord,
         0, 2, GL_FLOAT, 0 );
}


/**
 * @brief Gets the colour from a character.
 */
static const glColour* gl_fontGetColour( uint32_t ch )
{
   const glColour *col;
   switch (ch) {
      /* TOP SECRET COLOUR CONVENTION
       * FOR YOUR EYES ONLY
       *
       * Lowercase characters represent base colours.
       * Uppercase characters reperesent fancy game related colours.
       * Digits represent states.
       */
      /* Colours. */
      case 'r': col = &cFontRed; break;
      case 'g': col = &cFontGreen; break;
      case 'b': col = &cFontBlue; break;
      case 'o': col = &cFontOrange; break;
      case 'y': col = &cFontYellow; break;
      case 'w': col = &cFontWhite; break;
      case 'p': col = &cFontPurple; break;
      case 'n': col = &cFontGrey; break;
      /* Fancy states. */
      case 'F': col = &cFriend; break; /**< Friendly */
      case 'H': col = &cHostile; break; /**< Hostile */
      case 'N': col = &cNeutral; break; /**< Neutral */
      case 'I': col = &cInert; break; /**< Inert */
      case 'R': col = &cRestricted; break; /**< Restricted */
      case 'C': col = &cFontGreen; break; /**< Console */
      case '0': col = NULL; break;
      default:
         WARN("Unknown font escape code '%c'", ch);
         col = NULL;
         break;
   }
   return col;
}

/**
 * @brief Hash function for integers.
 *
 * Taken from http://burtleburtle.net/bob/hash/integer.html (Thomas Wang).
 * Meant to only use the low bits, not the high bits.
 */
static uint32_t hashint( uint32_t a )
{
   a += ~(a<<15);
   a ^=  (a>>10);
   a +=  (a<<3);
   a ^=  (a>>6);
   a += ~(a<<11);
   a ^=  (a>>16);
   return a;
}

/**
 * @brief Gets or caches a glyph to render.
 */
static glFontGlyph* gl_fontGetGlyph( glFontStash *stsh, uint32_t ch )
{
   int i;
   unsigned int h;

   /* Use hash table and linked lists to find the glyph. */
   h = hashint(ch) & (HASH_LUT_SIZE-1);
   i = stsh->lut[h];
   while (i != -1) {
      if (stsh->glyphs[i].codepoint == ch)
         return &stsh->glyphs[i];
      i = stsh->glyphs[i].next;
   }

   /* Glyph not found, have to generate. */
   glFontGlyph *glyph;
   font_char_t ft_char;
   int idx;

   /* Load data from freetype. */
   if (font_makeChar( stsh, &ft_char, ch ))
      return NULL;

   /* Create new character. */
   glyph = &array_grow( &stsh->glyphs );
   glyph->codepoint = ch;
   glyph->tex   = NULL;
   glyph->adv_x = ft_char.adv_x;
   glyph->adv_y = ft_char.adv_y;
   glyph->next  = -1;
   idx = glyph - stsh->glyphs;

   /* Insert in linked list. */
   i = stsh->lut[h];
   if (i == -1) {
      stsh->lut[h] = idx;
   }
   else {
      while (i != -1) {
         if (stsh->glyphs[i].next == -1) {
            stsh->glyphs[i].next = idx;
            break;
         }
         i = stsh->glyphs[i].next;
      }
   }

   /* Find empty texture and render char. */
   gl_fontAddGlyphTex( stsh, &ft_char, glyph );

   free(ft_char.data);

   return glyph;
}


/**
 * @brief Renders a character.
 */
static int gl_fontRenderGlyph( glFontStash* stsh, uint32_t ch, const glColour *c, int state, const int forceColor )
{
   double a;
   const glColour *col;

   /* Handle escape sequences. */
   if (ch == '\a') {/* Start sequence. */
      return 1;
   }
   if (state == 1) {
      if (!forceColor) {
         col = gl_fontGetColour( ch );
         a   = (c==NULL) ? 1. : c->a;
         if (col == NULL) {
            if (c==NULL)
               gl_uniformColor(shaders.font.color, &cWhite);
            else
               gl_uniformColor(shaders.font.color, c);
         }
         else
            gl_uniformAColor(shaders.font.color, col, a);
         font_lastCol = col;
      }
      return 0;
   }

   /* Unicode goes here.
    * First try to find the glyph. */
   glFontGlyph *glyph;
   glyph = gl_fontGetGlyph( stsh, ch );
   if (glyph == NULL) {
      WARN(_("Unable to find glyph '%d'!"), ch );
      return -1;
   }

   /* Activate texture. */
   glBindTexture(GL_TEXTURE_2D, glyph->tex->id);

   gl_Matrix4_Uniform(shaders.font.projection, font_projection_mat);

   /* Draw the element. */
   glDrawArrays( GL_TRIANGLE_STRIP, glyph->vbo_id, 4 );

   /* Translate matrix. */
   font_projection_mat = gl_Matrix4_Translate( font_projection_mat,
         glyph->adv_x, glyph->adv_y, 0 );

   return 0;
}


/**
 * @brief Ends the rendering engine.
 */
static void gl_fontRenderEnd (void)
{
   glDisableVertexAttribArray( shaders.font.vertex );
   glDisableVertexAttribArray( shaders.font.tex_coord );
   glUseProgram(0);

   /* Check for errors. */
   gl_checkErr();
}


/**
 * @brief Initializes a font.
 *
 *    @param font Font to load (NULL defaults to gl_defFont).
 *    @param fname Name of the font (from inside packfile).
 *    @param h Height of the font to generate.
 *    @param prefix Prefix to look for the font.
 *    @param flags Flags to use when generating the font.
 *    @return 0 on success.
 */
int gl_fontInit( glFont* font, const char *fname, const unsigned int h, const char *prefix, unsigned int flags )
{
   FT_Library library;
   FT_Face face;
   size_t bufsize;
   FT_Byte* buf;
   size_t i;
   glFontStash *stsh;
   glFontStashFreetype *ft;
   int ch;
   char fullname[PATH_MAX];

   /* Replace name if NULL. */
   if (fname == NULL)
      fname = FONT_DEFAULT_PATH;

   /* Get font stash. */
   if (avail_fonts==NULL)
      avail_fonts = array_create( glFontStash );

   /* Check if available. */
   if (!(flags & FONT_FLAG_DONTREUSE)) {
      for (i=0; i<(size_t)array_size(avail_fonts); i++) {
         stsh = &avail_fonts[i];
         if (stsh->h != h)
            continue;
         if (strcmp(stsh->fname,fname)!=0)
            continue;
         /* Found a match! */
         stsh->refcount++;
         font->id = stsh - avail_fonts;
         font->h = h;
         return 0;
      }
   }

   /* Create new font. */
   stsh = &array_grow( &avail_fonts );
   memset( stsh, 0, sizeof(glFontStash) );
   stsh->refcount = 1; /* Initialize refcount. */
   stsh->fname = strdup(fname);
   font->id = stsh - avail_fonts;
   font->h = h;

   /* Default sizes. */
   stsh->tw = DEFAULT_TEXTURE_SIZE;
   stsh->th = DEFAULT_TEXTURE_SIZE;
   stsh->h = font->h;

   /* Set up font stuff for next glyphs. */
   stsh->ft = array_create( glFontStashFreetype );
   ch = 0;
   for (i=0; i<strlen(fname)+1; i++) {
      if ((fname[i]=='\0') || (fname[i]==',')) {
         /* Set up name. */
         ft = &array_grow( &stsh->ft );
         ft->fontname = malloc( i-ch+1 );
         strncpy( ft->fontname, &fname[ch], i-ch );
         ft->fontname[i-ch] = '\0';

         /* Read font file. */
         nsnprintf( fullname, PATH_MAX, "%s%s", prefix, ft->fontname );
         //buf = ndata_read( fullname, &bufsize );
         buf = (FT_Byte*)_nfile_readFile( &bufsize, fullname );
         if (buf == NULL) {
            WARN(_("Unable to read font: %s"), ft->fontname);
            return -1;
         }

         /* Create a FreeType font library. */
         if (FT_Init_FreeType(&library)) {
            WARN(_("FT_Init_FreeType failed with font %s."), ft->fontname);
            return -1;
         }

         /* Object which freetype uses to store font info. */
         if (FT_New_Memory_Face( library, buf, bufsize, 0, &face )) {
            WARN(_("FT_New_Face failed loading library from %s"), ft->fontname);
            return -1;
         }

         /* Try to resize. */
         if (FT_IS_SCALABLE(face)) {
            if (FT_Set_Char_Size( face,
                     0, /* Same as width. */
                     h << 6, /* In 1/64th of a pixel. */
                     96, /* Create at 96 DPI */
                     96)) /* Create at 96 DPI */
               WARN(_("FT_Set_Char_Size failed."));
         }
         else
            WARN(_("Font isn't resizable!"));

         /* Select the character map. */
         if (FT_Select_Charmap( face, FT_ENCODING_UNICODE ))
            WARN(_("FT_Select_Charmap failed to change character mapping."));

         /* Save stuff. */
         ft->face     = face;
         ft->library  = library;
         ft->fontdata = buf;
         ch = i;
         if (fname[i]==',')
            ch++;
      }
   }

   /* Initialize the unicode support. */
   for (i=0; i<HASH_LUT_SIZE; i++)
      stsh->lut[i] = -1;
   stsh->glyphs = array_create( glFontGlyph );
   stsh->tex    = array_create( glFontTex );

   /* Set up VBOs. */
   stsh->mvbo = 256;
   stsh->vbo_tex_data  = calloc( 8*stsh->mvbo, sizeof(GLfloat) );
   stsh->vbo_vert_data = calloc( 8*stsh->mvbo, sizeof(GLshort) );
   stsh->vbo_tex  = gl_vboCreateStatic( sizeof(GLfloat)*8*stsh->mvbo,  stsh->vbo_tex_data );
   stsh->vbo_vert = gl_vboCreateStatic( sizeof(GLshort)*8*stsh->mvbo, stsh->vbo_vert_data );

   /* Initializes ASCII. */
   for (i=0; i<128; i++)
      if (isprint(i)) /* Only care about printables. */
         gl_fontGetGlyph( stsh, i );

   return 0;
}

/**
 * @brief Frees a loaded font.
 *
 *    @param font Font to free.
 */
void gl_freeFont( glFont* font )
{
   int i;
   glFontStashFreetype *ft;

   if (font == NULL)
      font = &gl_defFont;
   glFontStash *stsh = gl_fontGetStash( font );

   /* Check references. */
   stsh->refcount--;
   if (stsh->refcount > 0)
      return;
   /* Not references and must eliminate. */

   for (i=0; i<array_size(stsh->ft); i++) {
      ft = &stsh->ft[i];
      free(ft->fontname);
      FT_Done_Face(ft->face);
      free(ft->fontdata);
   }
   array_free( stsh->ft );

   free( stsh->fname );
   for (i=0; i<array_size(stsh->tex); i++)
      glDeleteTextures( 1, &stsh->tex->id );
   array_free( stsh->tex );
   stsh->tex = NULL;

   if (stsh->glyphs != NULL)
      array_free( stsh->glyphs );
   stsh->glyphs = NULL;
   gl_vboDestroy(stsh->vbo_tex);
   stsh->vbo_tex = NULL;
   gl_vboDestroy(stsh->vbo_vert);
   stsh->vbo_vert = NULL;
   free(stsh->vbo_tex_data);
   stsh->vbo_tex_data = NULL;
   free(stsh->vbo_vert_data);
   stsh->vbo_vert_data = NULL;
}


