/**************************************************************************\
 *
 *  This file is part of the Coin 3D visualization library.
 *  Copyright (C) 1998-2005 by Systems in Motion.  All rights reserved.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  ("GPL") version 2 as published by the Free Software Foundation.
 *  See the file LICENSE.GPL at the root directory of this source
 *  distribution for additional information about the GNU GPL.
 *
 *  For using Coin with software that can not be combined with the GNU
 *  GPL, and for taking advantage of the additional benefits of our
 *  support services, please contact Systems in Motion about acquiring
 *  a Coin Professional Edition License.
 *
 *  See <URL:http://www.coin3d.org/> for more information.
 *
 *  Systems in Motion, Postboks 1283, Pirsenteret, 7462 Trondheim, NORWAY.
 *  <URL:http://www.sim.no/>.
 *
\**************************************************************************/

/*!
  \class SoVBO
  \brief The SoVBO class is used to handle OpenGL vertex buffer objects.

  It wraps the buffer handling, taking care of multi-context handling
  and allocation/deallocation of buffers. FIXME: more doc.
  
*/

#include "SoVBO.h"
#include <Inventor/misc/SoContextHandler.h>
#include <Inventor/elements/SoGLCacheContextElement.h>
#include <Inventor/C/tidbits.h>
#include <Inventor/C/tidbitsp.h>
#include <Inventor/SbVec3f.h>
#include <Inventor/C/threads/threadsutilp.h>
#include <Inventor/C/glue/glp.h>
#include "SoVertexArrayIndexer.h"
#include "../share/gl/CoinGLPerformance.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

static int vbo_vertex_count_min_limit = -1;
static int vbo_vertex_count_max_limit = -1;
static SbHash <SbBool, uint32_t> * vbo_isfast_hash;

/*!
  Constructor
*/
SoVBO::SoVBO(const GLenum target, const GLenum usage)
  : target(target),
    usage(usage),
    data(NULL),
    datasize(0),
    dataid(0),
    didalloc(FALSE),
    vbohash(5)
{
  SoContextHandler::addContextDestructionCallback(context_destruction_cb, this);
}


/*!
  Destructor
*/
SoVBO::~SoVBO()
{
  SoContextHandler::removeContextDestructionCallback(context_destruction_cb, this);
  // schedule delete for all allocated GL resources
  this->vbohash.apply(vbo_schedule, NULL);
  if (this->didalloc) {
    char * ptr = (char*) this->data;
    delete[] ptr;
  }
}

// atexit cleanup function
static void vbo_atexit_cleanup(void)
{
  delete vbo_isfast_hash;
  vbo_isfast_hash = NULL;
}

void 
SoVBO::init(void)
{
  coin_glglue_add_instance_created_callback(context_created, NULL);

  vbo_isfast_hash = new SbHash <SbBool, uint32_t> (3);
  coin_atexit(vbo_atexit_cleanup, 0);
}

/*!
  Used to allocate buffer data. The user is responsible for filling in
  the correct type of data in the buffer before the buffer is used.

  \sa setBufferData()
*/
void *
SoVBO::allocBufferData(intptr_t size, uint32_t dataid)
{
  // schedule delete for all allocated GL resources
  this->vbohash.apply(vbo_schedule, NULL);
  // clear hash table
  this->vbohash.clear();

  if (this->didalloc && this->datasize == size) {
    return (void*)this->data;
  }
  if (this->didalloc) {
    char * ptr = (char*) this->data;
    delete[] ptr;
  }

  char * ptr = new char[size];
  this->didalloc = TRUE;
  this->data = (const GLvoid*) ptr;
  this->datasize = size;
  this->dataid = dataid;
  return (void*) this->data;
}

/*!
  Sets the buffer data. \a dataid is a unique id used to identify 
  the buffer data. In Coin it's possible to use the node id
  (SoNode::getNodeId()) to test if a buffer is valid for a node.
*/
void 
SoVBO::setBufferData(const GLvoid * data, intptr_t size, uint32_t dataid)
{
  // schedule delete for all allocated GL resources
  this->vbohash.apply(vbo_schedule, NULL);
  // clear hash table
  this->vbohash.clear();

  // clean up old buffer (if any)
  if (this->didalloc) {
    char * ptr = (char*) this->data;
    delete[] ptr;
  }
  
  this->data = data;
  this->datasize = size;
  this->dataid = dataid;
  this->didalloc = FALSE;
}

/*!
  Returns the buffer data id. 
  
  \sa setBufferData()
*/
uint32_t 
SoVBO::getBufferDataId(void) const
{
  return this->dataid;
}

/*!
  Returns the data pointer and size.
*/
void 
SoVBO::getBufferData(const GLvoid *& data, intptr_t & size)
{
  data = this->data;
  size = this->datasize;
}


/*!
  Binds the buffer for the context \a contextid.
*/
void 
SoVBO::bindBuffer(uint32_t contextid)
{
  if ((this->data == NULL) ||
      (this->datasize == 0)) {
    assert(0 && "no data in buffer");
    return;
  }

  const cc_glglue * glue = cc_glglue_instance((int) contextid);

  GLuint buffer;
  if (!this->vbohash.get(contextid, buffer)) {
    // need to create a new buffer for this context
    cc_glglue_glGenBuffers(glue, 1, &buffer);
    cc_glglue_glBindBuffer(glue, this->target, buffer);
    cc_glglue_glBufferData(glue, this->target,
                           this->datasize,
                           this->data,
                           this->usage);
    this->vbohash.put(contextid, buffer);
  }
  else {
    // buffer already exists, bind it
    cc_glglue_glBindBuffer(glue, this->target, buffer);
  }
}

//
// Callback from SbHash
//
void 
SoVBO::vbo_schedule(const uint32_t & key,
                    const GLuint & value,
                    void * closure)
{
  void * ptr = (void*) ((uintptr_t) value);
  SoGLCacheContextElement::scheduleDeleteCallback(key, vbo_delete, ptr);
}

//
// Callback from SoGLCacheContextElement
//
void 
SoVBO::vbo_delete(void * closure, uint32_t contextid)
{
  const cc_glglue * glue = cc_glglue_instance((int) contextid);
  GLuint id = (GLuint) ((uintptr_t) closure);
  cc_glglue_glDeleteBuffers(glue, 1, &id);
}

//
// Callback from SoContextHandler
//
void 
SoVBO::context_destruction_cb(uint32_t context, void * userdata)
{
  GLuint buffer;
  SoVBO * thisp = (SoVBO*) userdata;

  if (thisp->vbohash.get(context, buffer)) {
    const cc_glglue * glue = cc_glglue_instance((int) context);
    cc_glglue_glDeleteBuffers(glue, 1, &buffer);    
    thisp->vbohash.remove(context);
  }
}


/*!
  Sets the global limits on the number of vertex data in a node before
  vertex buffer objects are considered to be used for rendering.
*/
void 
SoVBO::setVertexCountLimits(const int minlimit, const int maxlimit)
{
  vbo_vertex_count_min_limit = minlimit;
  vbo_vertex_count_max_limit = maxlimit;
}

/*!
  Returns the vertex VBO minimum limit.

  \sa setVertexCountLimits()
 */
int 
SoVBO::getVertexCountMinLimit(void)
{
  if (vbo_vertex_count_min_limit < 0) {
    const char * env = coin_getenv("COIN_VBO_MIN_LIMIT");
    if (env) {
      vbo_vertex_count_min_limit = atoi(env);
    }
    else {
      vbo_vertex_count_min_limit = 40;
    }
  } 
  return vbo_vertex_count_min_limit;
}

/*!
  Returns the vertex VBO maximum limit.

  \sa setVertexCountLimits()
 */
int 
SoVBO::getVertexCountMaxLimit(void)
{
  if (vbo_vertex_count_max_limit < 0) {
    const char * env = coin_getenv("COIN_VBO_MAX_LIMIT");
    if (env) {
      vbo_vertex_count_max_limit = atoi(env);
    }
    else {
      vbo_vertex_count_max_limit = 10000000;
    }
  }
  return vbo_vertex_count_max_limit;
}

SbBool 
SoVBO::shouldCreateVBO(const uint32_t contextid, const int numdata)
{
  int minv = SoVBO::getVertexCountMinLimit();
  int maxv = SoVBO::getVertexCountMaxLimit();
  return (numdata >= minv) && (numdata <= maxv) && SoVBO::isVBOFast(contextid);
}


SbBool 
SoVBO::isVBOFast(const uint32_t contextid)
{
  SbBool result = TRUE;
  assert(vbo_isfast_hash != NULL);
  (void) vbo_isfast_hash->get(contextid, result);
  return result;
}

//
// callback from glglue (when a new glglue instance is created)
//
void 
SoVBO::context_created(const cc_glglue * glue, void * closure)
{
  SoVBO::testGLPerformance(coin_glglue_get_contextid(glue));
}

/* **********************************************************************************************/

typedef struct {
  SoVBO * vbo;
  SbVec3f * vertexarray;
  int size;
  SoVertexArrayIndexer * indexer;
  uint32_t contextid;
} vbo_performance_test_data;

// how many times the geometry is rendered in the performance test callbacks
#define PERF_NUM_LOOP 5

// rendering loop used to test vertex array and VBO rendering speed
static void 
vbo_performance_test(const cc_glglue * glue,
                     const vbo_performance_test_data * data,
                     const SbBool dovbo)
{
  const GLvoid * dataptr = NULL;
  if (dovbo) {
    data->vbo->bindBuffer(data->contextid);
  }
  else {
    dataptr = (const GLvoid*) data->vertexarray;
  }
  cc_glglue_glVertexPointer(glue, 3, GL_FLOAT, 0,
                            dataptr);
  cc_glglue_glEnableClientState(glue, GL_VERTEX_ARRAY);

  for (int i = 0; i < PERF_NUM_LOOP; i++) {
    data->indexer->render(glue, dovbo, data->contextid);
  }
  cc_glglue_glDisableClientState(glue, GL_VERTEX_ARRAY);
  if (dovbo) {
    cc_glglue_glBindBuffer(glue, GL_ARRAY_BUFFER, 0);
  }
}

// callback to test VBO rendering speed
static void 
vbo_performance_test_vbo(const cc_glglue * glue, void * closure)
{
  vbo_performance_test_data * data = (vbo_performance_test_data *) closure;
  vbo_performance_test(glue, data, TRUE);
}

// callback to test vertex array rendering speed
static void 
vbo_performance_test_va(const cc_glglue * glue, void * closure)
{
  vbo_performance_test_data * data = (vbo_performance_test_data *) closure;
  vbo_performance_test(glue, data, FALSE);
}

// callback to test immediate mode rendering speed
static void 
vbo_performance_test_immediate(const cc_glglue * glue, void * closure)
{
  vbo_performance_test_data * data = (vbo_performance_test_data *) closure;
  
  int x, y;
  int size = data->size;
  SbVec3f * vertexarray = data->vertexarray;

  for (int i = 0; i < PERF_NUM_LOOP; i++) {
#define IDX(ix, iy) ((iy)*size+ix)
    glBegin(GL_TRIANGLES);
    for (y = 0; y < size-1; y++) {
      for (x = 0; x < size-1; x++) {
        glVertex3fv((const GLfloat*) &vertexarray[IDX(x,y)]);
        glVertex3fv((const GLfloat*)&vertexarray[IDX(x+1,y)]);
        glVertex3fv((const GLfloat*)&vertexarray[IDX(x+1,y+1)]);
        
        glVertex3fv((const GLfloat*)&vertexarray[IDX(x,y)]);
        glVertex3fv((const GLfloat*)&vertexarray[IDX(x+1,y+1)]);
        glVertex3fv((const GLfloat*)&vertexarray[IDX(x,y+1)]);
      }
    }
    glEnd();
#undef IDX    
  }
}

#undef PERF_NUM_LOOP

//
// test OpenGL performance for a context.
//
void 
SoVBO::testGLPerformance(const uint32_t contextid)
{
  SbBool isfast = FALSE;
  // did we alreay test this for this context?
  assert(vbo_isfast_hash != NULL);
  if (vbo_isfast_hash->get(contextid, isfast)) return;

  const cc_glglue * glue = cc_glglue_instance(contextid);
  if (cc_glglue_has_vertex_buffer_object(glue)) {
    // create a regular grid with 256x256 points to test the performance on
    const int size = 256;
    const int half = size / 2;
    SbVec3f * vertexarray = new SbVec3f[size*size];
    SoVertexArrayIndexer * idx = new SoVertexArrayIndexer;
    SbVec3f * ptr = vertexarray;

    int x, y;
    
    for (y = 0; y < size; y++) {
      for (x = 0; x < size; x++) {
        ptr->setValue(float(x-half)/float(size)*0.1f, float(y-half)/float(size)*0.1f, 4.0f);
      }
    }
#define IDX(ix, iy) ((iy)*size+ix)
    for (y = 0; y < size-1; y++) {
      for (x = 0; x < size-1; x++) {
        idx->addTriangle(IDX(x,y), IDX(x+1,y), IDX(x+1, y+1));
        idx->addTriangle(IDX(x,y), IDX(x+1,y+1), IDX(x, y+1));
      }
    }
#undef IDX
    idx->close();
    SoVBO * vbo = new SoVBO();
    vbo->setBufferData(vertexarray, size*size*sizeof(SbVec3f), 0);
    vbo_performance_test_data data;
    data.vbo = vbo;
    data.vertexarray = vertexarray;
    data.indexer = idx;    
    data.contextid = contextid;
    data.size = size;
    // bind buffer first to create VBO
    vbo->bindBuffer(contextid);
    // unset VBO buffer before rendering
    cc_glglue_glBindBuffer(glue, GL_ARRAY_BUFFER, 0);

    CC_PERF_RENDER_CB * rendercbs[] = {
      vbo_performance_test_immediate,
      vbo_performance_test_va,
      vbo_performance_test_vbo
    };
    double averagerendertime[3];
    cc_perf_gl_timer(glue,
                     3,
                     rendercbs,
                     averagerendertime,
                     NULL, NULL, 10, SbTime(0.2),
                     &data);
    delete vbo;
    delete idx;
    delete[] vertexarray;

    // VBO is considered to be fast if it's 1.5 times faster than vertex
    // array and immediate mode rendering.
    if ((1.5f * averagerendertime[2] < averagerendertime[0]) &&
        (1.5f * averagerendertime[2] < averagerendertime[1])) isfast = TRUE;

  }
  vbo_isfast_hash->put(contextid, isfast);
}


